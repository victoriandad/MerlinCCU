#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "el320_raster.pio.h"

static constexpr uint PIN_BASE = 2;   // GPIO2..GPIO5 = VID,VCLK,HS,VS

static constexpr int FB_WIDTH  = 320;
static constexpr int FB_HEIGHT = 256;
static constexpr int FB_STRIDE = FB_WIDTH / 8;
static constexpr int FB_SIZE   = FB_STRIDE * FB_HEIGHT;

static constexpr int H_PRE_BLANK   = 32;
static constexpr int H_POST_BLANK  = 32;

static constexpr int V_SYNC_PULSE  = 2;
static constexpr int V_PRE_BLANK   = 2;
static constexpr int V_POST_BLANK  = 2;

static constexpr float CLKDIV      = 2.51f;

static constexpr int PIXELS_PER_LINE = H_PRE_BLANK + FB_WIDTH + H_POST_BLANK;
static constexpr int STEPS_PER_LINE  = PIXELS_PER_LINE * 2;   // VCLK low + high
static constexpr int STEPS_PER_WORD  = 8;                    // 8 nibbles in a 32-bit word
static constexpr int WORDS_PER_LINE  = STEPS_PER_LINE / STEPS_PER_WORD;
static constexpr int TOTAL_LINES     = V_SYNC_PULSE + V_PRE_BLANK + FB_HEIGHT + V_POST_BLANK;
static constexpr int RASTER_WORDS    = WORDS_PER_LINE * TOTAL_LINES;

static_assert((STEPS_PER_LINE % STEPS_PER_WORD) == 0, "Line packing must be exact");

static constexpr uint8_t BIT_VID  = 1u << 0;
static constexpr uint8_t BIT_VCLK = 1u << 1;
static constexpr uint8_t BIT_HS   = 1u << 2;
static constexpr uint8_t BIT_VS   = 1u << 3;

// Swing buffers
static uint8_t fb_a[FB_SIZE];
static uint8_t fb_b[FB_SIZE];

static uint8_t* fb_front = fb_a;
static uint8_t* fb_back  = fb_b;

// DMA/PIO raster stream
static uint32_t raster_words[RASTER_WORDS];

static int dma_chan_data;
static int dma_chan_ctrl;

// -----------------------------------------------------------------------------
// PIO / DMA setup
// -----------------------------------------------------------------------------

static void el320_raster_program_init(PIO pio, uint sm, uint offset, uint pin_base)
{
    for (uint pin = pin_base; pin < pin_base + 4; ++pin) {
        pio_gpio_init(pio, pin);
    }

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 4, true);

    pio_sm_config c = el320_raster_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin_base, 4);
    sm_config_set_out_shift(&c, true, true, 32);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_clkdiv(pio, sm, CLKDIV);
    pio_sm_set_enabled(pio, sm, true);
}

static void start_dma(PIO pio, uint sm)
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
        raster_words,
        RASTER_WORDS,
        false
    );

    dma_channel_config c_ctrl = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&c_ctrl, false);
    channel_config_set_write_increment(&c_ctrl, false);
    channel_config_set_chain_to(&c_ctrl, dma_chan_data);

    static const void* read_addr = raster_words;

    dma_channel_configure(
        dma_chan_ctrl,
        &c_ctrl,
        &dma_hw->ch[dma_chan_data].read_addr,
        &read_addr,
        1,
        false
    );

    dma_start_channel_mask(1u << dma_chan_data);
}

// -----------------------------------------------------------------------------
// Framebuffer helpers
// -----------------------------------------------------------------------------

static inline void fb_clear(uint8_t* fb, bool on)
{
    memset(fb, on ? 0xFF : 0x00, FB_SIZE);
}

static inline void fb_set_pixel(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) {
        return;
    }

    uint8_t& byte = fb[y * FB_STRIDE + (x >> 3)];
    uint8_t mask = 0x80u >> (x & 7);

    if (on) {
        byte |= mask;
    } else {
        byte &= ~mask;
    }
}

static inline bool fb_get_pixel(const uint8_t* fb, int x, int y)
{
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) {
        return false;
    }

    const uint8_t byte = fb[y * FB_STRIDE + (x >> 3)];
    const uint8_t mask = 0x80u >> (x & 7);
    return (byte & mask) != 0;
}

static void fb_draw_hline(uint8_t* fb, int x0, int x1, int y, bool on)
{
    if (y < 0 || y >= FB_HEIGHT) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(FB_WIDTH - 1, x1);

    for (int x = x0; x <= x1; ++x) {
        fb_set_pixel(fb, x, y, on);
    }
}

static void fb_draw_vline(uint8_t* fb, int x, int y0, int y1, bool on)
{
    if (x < 0 || x >= FB_WIDTH) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(FB_HEIGHT - 1, y1);

    for (int y = y0; y <= y1; ++y) {
        fb_set_pixel(fb, x, y, on);
    }
}

static void fb_draw_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;
    fb_draw_hline(fb, x, x + w - 1, y, on);
    fb_draw_hline(fb, x, x + w - 1, y + h - 1, on);
    fb_draw_vline(fb, x, y, y + h - 1, on);
    fb_draw_vline(fb, x + w - 1, y, y + h - 1, on);
}

static void fb_fill_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy) {
        fb_draw_hline(fb, x, x + w - 1, yy, on);
    }
}

static void fb_draw_diag(uint8_t* fb, bool on)
{
    const int limit = std::min(FB_WIDTH, FB_HEIGHT);
    for (int i = 0; i < limit; ++i) {
        fb_set_pixel(fb, i, i, on);
    }
}

// -----------------------------------------------------------------------------
// Raster builder
// -----------------------------------------------------------------------------

static inline void emit_step(uint32_t* buf, int& step_index, uint8_t nibble)
{
    const int word_index   = step_index / 8;
    const int nibble_index = step_index % 8;
    buf[word_index] |= (uint32_t)(nibble & 0x0F) << (nibble_index * 4);
    ++step_index;
}

static inline void emit_pixel(uint32_t* buf, int& step_index, bool vid, bool hs, bool vs)
{
    uint8_t base = 0;
    if (vid) base |= BIT_VID;
    if (hs)  base |= BIT_HS;
    if (vs)  base |= BIT_VS;

    // VCLK low then high
    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | BIT_VCLK);
}

static void build_blank_line(uint32_t* line_buf, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    // Pre-blank, HS high
    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    // Active region blank, HS high
    for (int i = 0; i < FB_WIDTH; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    // Post-blank, HS low
    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void build_fb_line(uint32_t* line_buf, const uint8_t* fb, int y, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    // Pre-blank, HS high
    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    // Active region from framebuffer
    for (int x = 0; x < FB_WIDTH; ++x) {
        const bool vid_on = fb_get_pixel(fb, x, y);
        emit_pixel(line_buf, step, vid_on, true, vs_high);
    }

    // Post-blank, HS low
    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void rebuild_raster_from_front_fb()
{
    int line = 0;

    // VS pulse lines
    for (int i = 0; i < V_SYNC_PULSE; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], true);
    }

    // Vertical pre-blank
    for (int i = 0; i < V_PRE_BLANK; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], false);
    }

    // Active image
    for (int y = 0; y < FB_HEIGHT; ++y, ++line) {
        build_fb_line(&raster_words[line * WORDS_PER_LINE], fb_front, y, false);
    }

    // Vertical post-blank
    for (int i = 0; i < V_POST_BLANK; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], false);
    }
}

// -----------------------------------------------------------------------------
// Swap
// -----------------------------------------------------------------------------

static void fb_swap()
{
    uint8_t* tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;
}

// -----------------------------------------------------------------------------
// Demo drawing
// -----------------------------------------------------------------------------

static void draw_demo_screen(uint8_t* fb)
{
    fb_clear(fb, false);

    fb_draw_rect(fb, 0, 0, FB_WIDTH, FB_HEIGHT, true);
    fb_draw_rect(fb, 10, 10, FB_WIDTH - 20, FB_HEIGHT - 20, true);

    fb_fill_rect(fb, 20, 20, 80, 40, true);
    fb_fill_rect(fb, 220, 30, 60, 60, true);

    fb_draw_diag(fb, true);

    for (int i = 0; i < 10; ++i) {
        fb_fill_rect(fb, 5, 20 + i * 22, 6, 10, true);               // left softkey markers
        fb_fill_rect(fb, FB_WIDTH - 11, 20 + i * 22, 6, 10, true);   // right softkey markers
    }

    // Bottom status strip
    fb_fill_rect(fb, 0, FB_HEIGHT - 16, FB_WIDTH, 16, true);
    fb_fill_rect(fb, 8, FB_HEIGHT - 12, 100, 8, false);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    printf("Framebuffer demo start. clkdiv=%.2f\n", CLKDIV);

    // Prepare initial back buffer content
    draw_demo_screen(fb_back);

    // Swap so demo becomes front buffer
    fb_swap();

    // Build raster stream from front buffer
    rebuild_raster_from_front_fb();

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);

    el320_raster_program_init(pio, sm, offset, PIN_BASE);
    start_dma(pio, sm);

    while (true) {
        tight_loop_contents();
    }

    return 0;
}