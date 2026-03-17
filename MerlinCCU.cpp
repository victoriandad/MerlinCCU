#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "el320_raster.pio.h"

static constexpr uint PIN_BASE = 2;   // GPIO2..GPIO5 = VID,VCLK,HS,VS

static constexpr int ACTIVE_PIXELS = 320;
static constexpr int ACTIVE_LINES  = 256;

static constexpr int H_PRE_BLANK   = 32;
static constexpr int H_POST_BLANK  = 32;

static constexpr int V_SYNC_PULSE  = 2;
static constexpr int V_PRE_BLANK   = 2;
static constexpr int V_POST_BLANK  = 2;

static constexpr float CLKDIV      = 2.51f;

static constexpr int PIXELS_PER_LINE = H_PRE_BLANK + ACTIVE_PIXELS + H_POST_BLANK;
static constexpr int STEPS_PER_LINE  = PIXELS_PER_LINE * 2;
static constexpr int STEPS_PER_WORD  = 8;
static constexpr int WORDS_PER_LINE  = STEPS_PER_LINE / STEPS_PER_WORD;
static constexpr int TOTAL_LINES     = V_SYNC_PULSE + V_PRE_BLANK + ACTIVE_LINES + V_POST_BLANK;
static constexpr int FRAME_WORDS     = WORDS_PER_LINE * TOTAL_LINES;

static_assert((STEPS_PER_LINE % STEPS_PER_WORD) == 0, "Line packing must be exact");

static constexpr uint8_t BIT_VID  = 1u << 0;
static constexpr uint8_t BIT_VCLK = 1u << 1;
static constexpr uint8_t BIT_HS   = 1u << 2;
static constexpr uint8_t BIT_VS   = 1u << 3;

static uint32_t frame_words[FRAME_WORDS];

static int dma_chan_data;
static int dma_chan_ctrl;

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

static inline void emit_step(uint32_t *buf, int &step_index, uint8_t nibble)
{
    const int word_index = step_index / 8;
    const int nibble_index = step_index % 8;
    buf[word_index] |= (uint32_t)(nibble & 0x0F) << (nibble_index * 4);
    ++step_index;
}

static inline void emit_pixel(uint32_t *buf, int &step_index, bool vid, bool hs, bool vs)
{
    uint8_t base = 0;
    if (vid) base |= BIT_VID;
    if (hs)  base |= BIT_HS;
    if (vs)  base |= BIT_VS;

    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | BIT_VCLK);
}

static void build_blank_line(uint32_t *line_buf, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;

    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int i = 0; i < ACTIVE_PIXELS; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void build_staircase_line(uint32_t *line_buf, int y, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;
    const int band = y / 32;

    for (int i = 0; i < H_PRE_BLANK; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int x = 0; x < ACTIVE_PIXELS; ++x) {
        const int word_index = x / 32;
        const bool vid_on = (band >= 0 && band <= 7 && word_index == band);
        emit_pixel(line_buf, step, vid_on, true, vs_high);
    }

    for (int i = 0; i < H_POST_BLANK; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

static void build_frame()
{
    int line = 0;

    for (int i = 0; i < V_SYNC_PULSE; ++i, ++line) {
        build_blank_line(&frame_words[line * WORDS_PER_LINE], true);
    }

    for (int i = 0; i < V_PRE_BLANK; ++i, ++line) {
        build_blank_line(&frame_words[line * WORDS_PER_LINE], false);
    }

    for (int y = 0; y < ACTIVE_LINES; ++y, ++line) {
        build_staircase_line(&frame_words[line * WORDS_PER_LINE], y, false);
    }

    for (int i = 0; i < V_POST_BLANK; ++i, ++line) {
        build_blank_line(&frame_words[line * WORDS_PER_LINE], false);
    }
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
        frame_words,
        FRAME_WORDS,
        false
    );

    dma_channel_config c_ctrl = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&c_ctrl, false);
    channel_config_set_write_increment(&c_ctrl, false);
    channel_config_set_chain_to(&c_ctrl, dma_chan_data);

    static const void *read_addr = frame_words;

    dma_channel_configure(
        dma_chan_ctrl,
        &c_ctrl,
        &dma_hw->ch[dma_chan_data].read_addr,
        &read_addr,
        1,
        false
    );

    dma_start_channel_mask((1u << dma_chan_data));
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);

    printf("CLKDIV=%.2f Hpre=%d Hpost=%d Vsync=%d Vpre=%d Vpost=%d\n",
           CLKDIV, H_PRE_BLANK, H_POST_BLANK, V_SYNC_PULSE, V_PRE_BLANK, V_POST_BLANK);

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);

    build_frame();
    el320_raster_program_init(pio, sm, offset, PIN_BASE);
    start_dma(pio, sm);

    while (true) {
        tight_loop_contents();
    }

    return 0;
}