#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "el320_raster.pio.h"

static constexpr uint PIN_BASE = 2;   // GPIO2..GPIO5 = VID,VCLK,HS,VS

// Native panel framebuffer dimensions (electrical scan orientation)
static constexpr int FB_WIDTH  = 320;
static constexpr int FB_HEIGHT = 256;
static constexpr int FB_STRIDE = FB_WIDTH / 8;
static constexpr int FB_SIZE   = FB_STRIDE * FB_HEIGHT;

// Logical UI dimensions (physical portrait mounting)
static constexpr int UI_WIDTH  = 252;
static constexpr int UI_HEIGHT = 320;

// Keep this at the known-good value
static constexpr int NATIVE_ROW_OFFSET = -4;

static constexpr int H_PRE_BLANK   = 32;
static constexpr int H_POST_BLANK  = 32;

static constexpr int V_SYNC_PULSE  = 2;
static constexpr int V_PRE_BLANK   = 2;
static constexpr int V_POST_BLANK  = 2;

static constexpr float CLKDIV      = 2.51f;

static constexpr int PIXELS_PER_LINE = H_PRE_BLANK + FB_WIDTH + H_POST_BLANK;
static constexpr int STEPS_PER_LINE  = PIXELS_PER_LINE * 2;
static constexpr int STEPS_PER_WORD  = 8;
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
// Native framebuffer helpers
// -----------------------------------------------------------------------------

static inline void fb_clear(uint8_t* fb, bool on)
{
    memset(fb, on ? 0xFF : 0x00, FB_SIZE);
}

static inline void fb_set_pixel_native(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) {
        return;
    }

    uint8_t& byte = fb[y * FB_STRIDE + (x >> 3)];
    uint8_t mask = 0x80u >> (x & 7);

    if (on) {
        byte |= mask;
    } else {
        byte &= static_cast<uint8_t>(~mask);
    }
}

static inline bool fb_get_pixel_native(const uint8_t* fb, int x, int y)
{
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) {
        return false;
    }

    const uint8_t byte = fb[y * FB_STRIDE + (x >> 3)];
    const uint8_t mask = 0x80u >> (x & 7);
    return (byte & mask) != 0;
}

// -----------------------------------------------------------------------------
// Logical portrait-space helpers
// -----------------------------------------------------------------------------

static inline int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline void fb_set_pixel(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return;
    }

    const int x_native = y;
    const int y_native_raw = FB_HEIGHT - 1 - x + NATIVE_ROW_OFFSET;
    const int y_native = clamp_int(y_native_raw, 0, FB_HEIGHT - 1);

    fb_set_pixel_native(fb, x_native, y_native, on);
}

static void fb_draw_hline(uint8_t* fb, int x0, int x1, int y, bool on)
{
    if (y < 0 || y >= UI_HEIGHT) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(UI_WIDTH - 1, x1);

    for (int x = x0; x <= x1; ++x) {
        fb_set_pixel(fb, x, y, on);
    }
}

static void fb_draw_vline(uint8_t* fb, int x, int y0, int y1, bool on)
{
    if (x < 0 || x >= UI_WIDTH) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(UI_HEIGHT - 1, y1);

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
    const int limit = std::min(UI_WIDTH, UI_HEIGHT);
    for (int i = 0; i < limit; ++i) {
        fb_set_pixel(fb, i, i, on);
    }
}

static void fb_draw_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool on)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        fb_set_pixel(fb, x0, y0, on);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
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

    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | BIT_VCLK);
}

static void build_blank_line(uint32_t* line_buf, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int i = 0; i < FB_WIDTH; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void build_fb_line(uint32_t* line_buf, const uint8_t* fb, int y, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int x = 0; x < FB_WIDTH; ++x) {
        const bool vid_on = fb_get_pixel_native(fb, x, y);
        emit_pixel(line_buf, step, vid_on, true, vs_high);
    }

    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void rebuild_raster_from_front_fb()
{
    int line = 0;

    for (int i = 0; i < V_SYNC_PULSE; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], true);
    }

    for (int i = 0; i < V_PRE_BLANK; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], false);
    }

    for (int y = 0; y < FB_HEIGHT; ++y, ++line) {
        build_fb_line(&raster_words[line * WORDS_PER_LINE], fb_front, y, false);
    }

    for (int i = 0; i < V_POST_BLANK; ++i, ++line) {
        build_blank_line(&raster_words[line * WORDS_PER_LINE], false);
    }
}

// -----------------------------------------------------------------------------
// Swap / present
// -----------------------------------------------------------------------------

static void fb_swap()
{
    uint8_t* tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;
}

static void present_backbuffer()
{
    fb_swap();
    rebuild_raster_from_front_fb();
}

// -----------------------------------------------------------------------------
// Demo screens in PORTRAIT logical coordinates
// -----------------------------------------------------------------------------

static void draw_demo_screen_1(uint8_t* fb)
{
    fb_clear(fb, false);

    fb_draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    fb_draw_rect(fb, 10, 10, UI_WIDTH - 20, UI_HEIGHT - 20, true);

    fb_fill_rect(fb, 20, 20, 60, 40, true);
    fb_fill_rect(fb, UI_WIDTH - 80, 30, 40, 70, true);

    fb_draw_diag(fb, true);

    for (int i = 0; i < 10; ++i) {
        fb_fill_rect(fb, 5, 20 + i * 28, 6, 12, true);
        fb_fill_rect(fb, UI_WIDTH - 11, 20 + i * 28, 6, 12, true);
    }

    fb_fill_rect(fb, 0, UI_HEIGHT - 16, UI_WIDTH, 16, true);
    fb_fill_rect(fb, 8, UI_HEIGHT - 12, 100, 8, false);
}

static void draw_demo_screen_2(uint8_t* fb)
{
    fb_clear(fb, false);

    fb_draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    fb_draw_rect(fb, 12, 12, UI_WIDTH - 24, UI_HEIGHT - 24, true);

    fb_fill_rect(fb, 0, 0, UI_WIDTH, 18, true);
    fb_fill_rect(fb, 8, 5, 70, 8, false);
    fb_fill_rect(fb, UI_WIDTH - 78, 5, 70, 8, false);

    for (int i = 0; i < 5; ++i) {
        fb_fill_rect(fb, 18, 35 + i * 48, 54, 18, true);
        fb_fill_rect(fb, UI_WIDTH - 72, 35 + i * 48, 54, 18, true);
    }

    fb_draw_rect(fb, 78, 60, 100, 170, true);
    fb_draw_line(fb, 78, 60, 177, 229, true);
    fb_draw_line(fb, 177, 60, 78, 229, true);

    fb_fill_rect(fb, 0, UI_HEIGHT - 20, UI_WIDTH, 20, true);
    fb_fill_rect(fb, 88, UI_HEIGHT - 14, 80, 8, false);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    printf("Working portrait baseline. clkdiv=%.2f row_offset=%d\n",
           CLKDIV, NATIVE_ROW_OFFSET);

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);

    draw_demo_screen_1(fb_back);
    present_backbuffer();

    el320_raster_program_init(pio, sm, offset, PIN_BASE);
    start_dma(pio, sm);

    bool show_first = false;

    while (true) {
        sleep_ms(3000);

        if (show_first) {
            draw_demo_screen_1(fb_back);
            printf("Presenting portrait demo screen 1\n");
        } else {
            draw_demo_screen_2(fb_back);
            printf("Presenting portrait demo screen 2\n");
        }

        present_backbuffer();
        show_first = !show_first;
    }

    return 0;
}