#include "display.h"

#include <array>
#include <cstring>

#include "hardware/dma.h"

#include "el320_raster.pio.h"
#include "framebuffer.h"
#include "panel_config.h"

namespace display
{

namespace
{

/// @brief Bit positions within one packed raster nibble.
/// @details The nibble layout mirrors the PIO pin order so raster generation can name each
/// signal explicitly instead of repeating anonymous shift literals through scanout code.
constexpr uint8_t kBitVid = 1U << 0;
constexpr uint8_t kBitVclk = 1U << 1;
constexpr uint8_t kBitHs = 1U << 2;
constexpr uint8_t kBitVs = 1U << 3;
using RasterBuffer = std::array<uint32_t, kRasterWords>;

// The raster buffers stay in static storage because DMA needs a stable source
// address for the lifetime of the scanout loop. Swapping pointers is cheaper
// and safer than rebuilding in place while the panel is being refreshed.
RasterBuffer g_raster_a = {};
RasterBuffer g_raster_b = {};

RasterBuffer* g_raster_front = &g_raster_a;
RasterBuffer* g_raster_back = &g_raster_b;
RasterBuffer* g_raster_pending = nullptr;

// The control DMA channel rewrites the data channel's read pointer from this
// indirection slot once per frame. That lets the ISR switch buffers by updating
// one pointer instead of touching DMA registers directly mid-transfer.
const void* g_dma_read_addr = g_raster_a.data();

int g_dma_chan_data = -1;
int g_dma_chan_ctrl = -1;
bool g_dma_running = false;
PIO g_active_pio = pio0;
uint g_active_sm = 0;
float g_configured_clkdiv = kPanel.clkdiv;

/// @brief DMA interrupt handler used to switch scanout buffers safely.
/// @details The data DMA channel streams one whole frame into the PIO TX FIFO.
/// A second control DMA channel then reloads the data channel's read address so
/// the same frame repeats forever. This interrupt fires at that frame boundary.
/// Buffer swaps happen here so the UI can render whenever it wants without
/// risking a torn frame half way down the panel.
void dma_ctrl_irq_handler()
{
    const uint32_t kMask = 1U << g_dma_chan_ctrl;
    dma_hw->ints0 = kMask;

    if (g_raster_pending != nullptr)
    {
        RasterBuffer* previous_front = g_raster_front;
        g_raster_front = g_raster_pending;
        g_raster_back = previous_front;
        g_raster_pending = nullptr;
        g_dma_read_addr = g_raster_front->data();
    }
}

/// @brief Clamps one integer into an inclusive range.
inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

/// @brief Appends one 4-bit output state to a packed raster buffer.
/// @details Each nibble contains one simultaneous state for VID, VCLK, HS and
/// VS. Eight nibbles are packed into one 32-bit word.
void emit_step(uint32_t* buf, int& step_index, uint8_t nibble)
{
    const int kWordIndex = step_index / 8;
    const int kNibbleIndex = step_index % 8;
    buf[kWordIndex] |= static_cast<uint32_t>(nibble & 0x0F) << (kNibbleIndex * 4);
    ++step_index;
}

/// @brief Emits one pixel time into the raster buffer.
/// @details Each pixel is represented by two steps: clock low, then clock high.
void emit_pixel(uint32_t* buf, int& step_index, bool vid, bool hs, bool vs)
{
    uint8_t base = 0;
    if (vid)
    {
        base |= kBitVid;
    }
    if (hs)
    {
        base |= kBitHs;
    }
    if (vs)
    {
        base |= kBitVs;
    }

    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | kBitVclk);
}

/// @brief Builds one blank native scan line including sync and blanking.
/// @details The panel expects valid HS timing even during blank regions, so the
/// blank line builder still emits the full horizontal structure rather than a
/// shorter "all off" placeholder.
void build_blank_line(uint32_t* line_buf, bool vs_high)
{
    std::memset(line_buf, 0, kWordsPerLine * sizeof(uint32_t));
    int step = 0;

    for (int i = 0; i < kPanel.h_pre_blank; ++i)
    {
        emit_pixel(line_buf, step, false, true, vs_high);
    }
    for (int i = 0; i < kFbWidth; ++i)
    {
        emit_pixel(line_buf, step, false, true, vs_high);
    }
    for (int i = 0; i < kPanel.h_post_blank; ++i)
    {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

/// @brief Builds one active native scan line from the UI framebuffer.
/// @details This is where portrait UI coordinates are mapped back into the
/// panel's native electrical scan order and row offset. The slightly odd
/// coordinate transform exists because the framebuffer is stored in the human-
/// friendly UI orientation, while the panel is scanned in its own native order.
void build_fb_line(uint32_t* line_buf, const uint8_t* fb, int y, bool vs_high)
{
    std::memset(line_buf, 0, kWordsPerLine * sizeof(uint32_t));
    int step = 0;
    const int kMaxUiSourceY = kFbHeight - 1 + kPanel.native_row_offset;
    const int kUiX = clamp_int(kMaxUiSourceY - y, 0, kUiWidth - 1);

    for (int i = 0; i < kPanel.h_pre_blank; ++i)
    {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int x = 0; x < kFbWidth; ++x)
    {
        const bool kVidOn =
            (x < kUiHeight) && (y <= kMaxUiSourceY) ? framebuffer::get_pixel(fb, kUiX, x) : false;
        emit_pixel(line_buf, step, kVidOn, true, vs_high);
    }

    for (int i = 0; i < kPanel.h_post_blank; ++i)
    {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

/// @brief Rebuilds a complete raster buffer from a UI framebuffer.
/// @details The raster is regenerated as a packed electrical frame up front so
/// the realtime scanout path can stay simple and deterministic once DMA starts.
void rebuild_raster_from_fb(const uint8_t* fb, uint32_t* raster)
{
    int line = 0;

    for (int i = 0; i < kPanel.v_sync_pulse; ++i, ++line)
    {
        build_blank_line(&raster[line * kWordsPerLine], true);
    }
    for (int i = 0; i < kPanel.v_pre_blank; ++i, ++line)
    {
        build_blank_line(&raster[line * kWordsPerLine], false);
    }
    for (int y = 0; y < kFbHeight; ++y, ++line)
    {
        build_fb_line(&raster[line * kWordsPerLine], fb, y, false);
    }
    for (int i = 0; i < kPanel.v_post_blank; ++i, ++line)
    {
        build_blank_line(&raster[line * kWordsPerLine], false);
    }
}

/// @brief Swaps the front and back raster pointers.
void raster_swap()
{
    RasterBuffer* tmp = g_raster_front;
    g_raster_front = g_raster_back;
    g_raster_back = tmp;
}

/// @brief Configures the PIO state machine that outputs the prepared raster.
/// @details The PIO program itself is intentionally minimal; almost all timing
/// complexity is precomputed into the raster buffer so scanout stays predictable
/// and easy to reason about during panel bring-up.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void el320_raster_program_init(PIO pio, uint sm, uint offset, uint pin_base)
{
    for (uint pin = pin_base; pin < pin_base + 4; ++pin)
    {
        pio_gpio_init(pio, pin);
    }

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 4, true);

    pio_sm_config c = el320_raster_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin_base, 4);
    sm_config_set_out_shift(&c, true, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_clkdiv(pio, sm, g_configured_clkdiv);
    pio_sm_set_enabled(pio, sm, true);
}

/// @brief Starts the repeating two-channel DMA scanout loop.
/// @details A second control channel is used here because the Pico DMA can
/// cheaply self-reload the data channel once per frame. That avoids CPU work in
/// the steady-state scanout path and keeps display timing stable.
void start_dma(PIO pio, uint sm)
{
    g_active_pio = pio;
    g_active_sm = sm;

    // The scanout loop uses two DMA channels: one streams the frame data into
    // the PIO TX FIFO, and the second re-arms that data channel once the frame
    // completes. Claiming them together keeps the relationship explicit.
    g_dma_chan_data = dma_claim_unused_channel(true);
    g_dma_chan_ctrl = dma_claim_unused_channel(true);

    // The data channel is the steady-state worker. It walks the packed raster
    // buffer word by word and writes each word into the PIO FIFO whenever the
    // state machine asserts its transmit DREQ.
    dma_channel_config c_data = dma_channel_get_default_config(g_dma_chan_data);
    channel_config_set_transfer_data_size(&c_data, DMA_SIZE_32);
    channel_config_set_read_increment(&c_data, true);
    channel_config_set_write_increment(&c_data, false);
    channel_config_set_dreq(&c_data, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c_data, g_dma_chan_ctrl);

    dma_channel_configure(g_dma_chan_data, &c_data, &pio->txf[sm], g_raster_front->data(),
                          kRasterWords, false);

    // The control channel performs a one-word transfer into the data channel's
    // read-address register. That is what turns the one-shot frame transfer
    // above into a repeating loop without the CPU having to restart DMA every
    // frame.
    dma_channel_config c_ctrl = dma_channel_get_default_config(g_dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&c_ctrl, false);
    channel_config_set_write_increment(&c_ctrl, false);
    channel_config_set_chain_to(&c_ctrl, g_dma_chan_data);

    // DMA expects a pointer to the read-address source slot itself so the
    // control channel can copy that stored pointer value into the data channel.
    const void* const* dma_read_addr_slot = &g_dma_read_addr;
    dma_channel_configure(g_dma_chan_ctrl, &c_ctrl, &dma_hw->ch[g_dma_chan_data].read_addr,
                          reinterpret_cast<const volatile void*>(dma_read_addr_slot), 1, false);

    // The control channel fires an interrupt at the frame boundary. That is the
    // safe point to adopt any newly prepared raster because the next frame has
    // not started streaming yet.
    dma_channel_set_irq0_enabled(g_dma_chan_ctrl, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_ctrl_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Starting only the data channel is enough: once it completes, the chain
    // relationship hands control to the reload channel, which in turn hands
    // back to the data channel for the next frame.
    const uint32_t kDataChannelMask = 1UL << static_cast<uint32_t>(g_dma_chan_data);
    dma_start_channel_mask(kDataChannelMask);
    g_dma_running = true;
}

} // namespace

/// @brief Initializes the display scanout path and starts continuous refresh.
void init(PIO pio, uint sm, uint offset, uint pin_base)
{
    el320_raster_program_init(pio, sm, offset, pin_base);
    start_dma(pio, sm);
}

/// @brief Applies a new PIO clock divider to the active display state machine.
void set_clkdiv(float clkdiv)
{
    g_configured_clkdiv = clkdiv;
    pio_sm_set_clkdiv(g_active_pio, g_active_sm, g_configured_clkdiv);
}

/// @brief Converts the supplied UI framebuffer into the inactive raster buffer.
/// @details If DMA is already running, the new raster is queued for adoption at
/// the next frame boundary. During early startup, before DMA begins, the raster
/// swap happens immediately. Deferring the pointer flip until the IRQ boundary
/// is what keeps scanout atomic from the panel's point of view.
void present(const uint8_t* ui_fb)
{
    rebuild_raster_from_fb(ui_fb, g_raster_back->data());

    if (!g_dma_running)
    {
        raster_swap();
        return;
    }

    g_raster_pending = g_raster_back;
}

} // namespace display
