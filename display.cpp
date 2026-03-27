#include "display.h"

#include <cstring>

#include "hardware/dma.h"

#include "el320_raster.pio.h"
#include "framebuffer.h"
#include "panel_config.h"

namespace display {

namespace {

constexpr uint8_t BIT_VID  = 1u << 0;
constexpr uint8_t BIT_VCLK = 1u << 1;
constexpr uint8_t BIT_HS   = 1u << 2;
constexpr uint8_t BIT_VS   = 1u << 3;

uint32_t raster_a[RASTER_WORDS];
uint32_t raster_b[RASTER_WORDS];

uint32_t* raster_front = raster_a;
uint32_t* raster_back = raster_b;
uint32_t* raster_pending = nullptr;

const void* dma_read_addr = raster_a;

int dma_chan_data = -1;
int dma_chan_ctrl = -1;
bool dma_running = false;

/// @brief DMA interrupt handler used to switch scanout buffers safely.
/// @details The data DMA channel streams one whole frame into the PIO TX FIFO.
/// A second control DMA channel then reloads the data channel's read address so
/// the same frame repeats forever. This interrupt fires at that frame boundary.
void dma_ctrl_irq_handler()
{
    const uint32_t mask = 1u << dma_chan_ctrl;
    dma_hw->ints0 = mask;

    if (raster_pending != nullptr) {
        uint32_t* previous_front = raster_front;
        raster_front = raster_pending;
        raster_back = previous_front;
        raster_pending = nullptr;
        dma_read_addr = raster_front;
    }
}

inline int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

/// @brief Appends one 4-bit output state to a packed raster buffer.
/// @details Each nibble contains one simultaneous state for VID, VCLK, HS and
/// VS. Eight nibbles are packed into one 32-bit word.
void emit_step(uint32_t* buf, int& step_index, uint8_t nibble)
{
    const int word_index = step_index / 8;
    const int nibble_index = step_index % 8;
    buf[word_index] |= static_cast<uint32_t>(nibble & 0x0F) << (nibble_index * 4);
    ++step_index;
}

/// @brief Emits one pixel time into the raster buffer.
/// @details Each pixel is represented by two steps: clock low, then clock high.
void emit_pixel(uint32_t* buf, int& step_index, bool vid, bool hs, bool vs)
{
    uint8_t base = 0;
    if (vid) base |= BIT_VID;
    if (hs)  base |= BIT_HS;
    if (vs)  base |= BIT_VS;

    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | BIT_VCLK);
}

/// @brief Builds one blank native scan line including sync and blanking.
void build_blank_line(uint32_t* line_buf, bool vs_high)
{
    std::memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    for (int i = 0; i < PANEL.h_pre_blank; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }
    for (int i = 0; i < FB_WIDTH; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }
    for (int i = 0; i < PANEL.h_post_blank; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

/// @brief Builds one active native scan line from the UI framebuffer.
/// @details This is where portrait UI coordinates are mapped back into the
/// panel's native electrical scan order and row offset.
void build_fb_line(uint32_t* line_buf, const uint8_t* fb, int y, bool vs_high)
{
    std::memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;
    const int max_ui_source_y = FB_HEIGHT - 1 + PANEL.native_row_offset;
    const int ui_x = clamp_int(max_ui_source_y - y, 0, UI_WIDTH - 1);

    for (int i = 0; i < PANEL.h_pre_blank; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int x = 0; x < FB_WIDTH; ++x) {
        const bool vid_on = (x < UI_HEIGHT) && (y <= max_ui_source_y)
            ? framebuffer::get_pixel(fb, ui_x, x)
            : false;
        emit_pixel(line_buf, step, vid_on, true, vs_high);
    }

    for (int i = 0; i < PANEL.h_post_blank; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

/// @brief Rebuilds a complete raster buffer from a UI framebuffer.
void rebuild_raster_from_fb(const uint8_t* fb, uint32_t* raster)
{
    int line = 0;

    for (int i = 0; i < PANEL.v_sync_pulse; ++i, ++line) {
        build_blank_line(&raster[line * WORDS_PER_LINE], true);
    }
    for (int i = 0; i < PANEL.v_pre_blank; ++i, ++line) {
        build_blank_line(&raster[line * WORDS_PER_LINE], false);
    }
    for (int y = 0; y < FB_HEIGHT; ++y, ++line) {
        build_fb_line(&raster[line * WORDS_PER_LINE], fb, y, false);
    }
    for (int i = 0; i < PANEL.v_post_blank; ++i, ++line) {
        build_blank_line(&raster[line * WORDS_PER_LINE], false);
    }
}

/// @brief Swaps the front and back raster pointers.
void raster_swap()
{
    uint32_t* tmp = raster_front;
    raster_front = raster_back;
    raster_back = tmp;
}

/// @brief Configures the PIO state machine that outputs the prepared raster.
void el320_raster_program_init(PIO pio, uint sm, uint offset, uint pin_base)
{
    for (uint pin = pin_base; pin < pin_base + 4; ++pin) {
        pio_gpio_init(pio, pin);
    }

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 4, true);

    pio_sm_config c = el320_raster_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin_base, 4);
    sm_config_set_out_shift(&c, true, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_clkdiv(pio, sm, PANEL.clkdiv);
    pio_sm_set_enabled(pio, sm, true);
}

/// @brief Starts the repeating two-channel DMA scanout loop.
void start_dma(PIO pio, uint sm)
{
    dma_chan_data = dma_claim_unused_channel(true);
    dma_chan_ctrl = dma_claim_unused_channel(true);

    dma_channel_config c_data = dma_channel_get_default_config(dma_chan_data);
    channel_config_set_transfer_data_size(&c_data, DMA_SIZE_32);
    channel_config_set_read_increment(&c_data, true);
    channel_config_set_write_increment(&c_data, false);
    channel_config_set_dreq(&c_data, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c_data, dma_chan_ctrl);

    dma_channel_configure(
        dma_chan_data,
        &c_data,
        &pio->txf[sm],
        raster_front,
        RASTER_WORDS,
        false
    );

    dma_channel_config c_ctrl = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&c_ctrl, false);
    channel_config_set_write_increment(&c_ctrl, false);
    channel_config_set_chain_to(&c_ctrl, dma_chan_data);

    dma_channel_configure(
        dma_chan_ctrl,
        &c_ctrl,
        &dma_hw->ch[dma_chan_data].read_addr,
        &dma_read_addr,
        1,
        false
    );

    dma_channel_set_irq0_enabled(dma_chan_ctrl, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_ctrl_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_start_channel_mask(1u << dma_chan_data);
    dma_running = true;
}

}  // namespace

void init(PIO pio, uint sm, uint offset, uint pin_base)
{
    el320_raster_program_init(pio, sm, offset, pin_base);
    start_dma(pio, sm);
}

/// @brief Converts the supplied UI framebuffer into the inactive raster buffer.
/// @details If DMA is already running, the new raster is queued for adoption at
/// the next frame boundary. During early startup, before DMA begins, the raster
/// swap happens immediately.
void present(const uint8_t* ui_fb)
{
    rebuild_raster_from_fb(ui_fb, raster_back);

    if (!dma_running) {
        raster_swap();
        return;
    }

    raster_pending = raster_back;
}

}  // namespace display
