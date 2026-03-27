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

/// @brief Describes one panel mounting and timing setup.
/// @details The firmware works with two coordinate systems:
/// - UI space: portrait-oriented coordinates used by drawing code.
/// - Native panel space: the electrical scan order expected by the display.
///
/// This struct keeps the panel-specific geometry and timing values together so
/// the rest of the code can treat them as one coherent configuration.
struct PanelConfig {
    int fb_width;
    int fb_height;
    int ui_width;
    int ui_height;
    int native_row_offset;
    int h_pre_blank;
    int h_post_blank;
    int v_sync_pulse;
    int v_pre_blank;
    int v_post_blank;
    float clkdiv;
};

/// @brief Panel timing and geometry for the current EL320 portrait mounting.
/// @details These are the current known-good settings for the display under
/// test. If the image is unstable, shifted, or clipped, start by reviewing
/// these values.
static constexpr PanelConfig PANEL = {
    .fb_width = 320,
    .fb_height = 256,
    .ui_width = 252,
    .ui_height = 320,
    .native_row_offset = -4,
    .h_pre_blank = 32,
    .h_post_blank = 32,
    .v_sync_pulse = 2,
    .v_pre_blank = 2,
    .v_post_blank = 2,
    .clkdiv = 2.51f,
};

// Native panel framebuffer dimensions (electrical scan orientation)
static constexpr int FB_WIDTH  = PANEL.fb_width;
static constexpr int FB_HEIGHT = PANEL.fb_height;

// Logical UI dimensions (physical portrait mounting)
static constexpr int UI_WIDTH  = PANEL.ui_width;
static constexpr int UI_HEIGHT = PANEL.ui_height;
static constexpr int UI_STRIDE = (UI_WIDTH + 7) / 8;
static constexpr int UI_FB_SIZE = UI_STRIDE * UI_HEIGHT;

static constexpr int PIXELS_PER_LINE = PANEL.h_pre_blank + FB_WIDTH + PANEL.h_post_blank;
static constexpr int STEPS_PER_LINE  = PIXELS_PER_LINE * 2;
static constexpr int STEPS_PER_WORD  = 8;
static constexpr int WORDS_PER_LINE  = STEPS_PER_LINE / STEPS_PER_WORD;
static constexpr int TOTAL_LINES     = PANEL.v_sync_pulse + PANEL.v_pre_blank + FB_HEIGHT + PANEL.v_post_blank;
static constexpr int RASTER_WORDS    = WORDS_PER_LINE * TOTAL_LINES;

static_assert((STEPS_PER_LINE % STEPS_PER_WORD) == 0, "Line packing must be exact");

static constexpr uint8_t BIT_VID  = 1u << 0;
static constexpr uint8_t BIT_VCLK = 1u << 1;
static constexpr uint8_t BIT_HS   = 1u << 2;
static constexpr uint8_t BIT_VS   = 1u << 3;

/// @brief Logical buttons the future CCU keypad can expose to the UI.
enum class ButtonId : uint8_t {
    LeftTop = 0,
    LeftUpper,
    LeftMiddle,
    LeftLower,
    LeftBottom,
    RightTop,
    RightUpper,
    RightMiddle,
    RightLower,
    RightBottom,
    Count,
};

/// @brief Simple edge event produced by the button debouncer.
enum class ButtonEventType : uint8_t {
    None = 0,
    Pressed,
    Released,
};

/// @brief One physical button wiring definition.
/// @details Set @c pin to a valid GPIO number when the hardware arrives.
/// A value of `-1` means the button is currently not wired and should be ignored.
struct ButtonConfig {
    ButtonId id;
    int pin;
    bool active_low;
    const char* name;
};

/// @brief Runtime debounce state for one logical button.
struct ButtonState {
    bool raw_level;
    bool stable_pressed;
    absolute_time_t last_change_time;
};

/// @brief Edge event returned by the input poller.
struct ButtonEvent {
    ButtonId id;
    ButtonEventType type;
};

/// @brief High-level content modes shown on the display.
/// @details The Life screensaver is the current default because it exercises
/// continuous screen updates and helps mask the panel's existing burn-in.
enum class ScreenMode : uint8_t {
    DemoPattern = 0,
    DummyMenu,
    LifeScreensaver,
};

static constexpr int BUTTON_DEBOUNCE_MS = 25;
static constexpr size_t BUTTON_COUNT = static_cast<size_t>(ButtonId::Count);
static constexpr int LIFE_SCALE = 2;
static constexpr int LIFE_WIDTH = UI_WIDTH / LIFE_SCALE;
static constexpr int LIFE_HEIGHT = UI_HEIGHT / LIFE_SCALE;
static constexpr int LIFE_CELL_COUNT = LIFE_WIDTH * LIFE_HEIGHT;
/// @brief Reseed timeout for a completely unchanged Life field.
static constexpr uint32_t LIFE_STABLE_RESEED_FRAMES = 200;
/// @brief Number of recent Life hashes remembered for repeat-cycle detection.
static constexpr size_t LIFE_HASH_HISTORY = 8;
/// @brief Reseed timeout for an oscillating Life field that keeps repeating.
static constexpr uint32_t LIFE_REPEAT_RESEED_FRAMES = 100;

/// @brief Placeholder keypad mapping.
/// @details This is a skeleton for future keypad support. Replace the `-1`
/// values with real GPIO numbers once the keypad wiring is known and connected.
static constexpr ButtonConfig BUTTONS[BUTTON_COUNT] = {
    {ButtonId::LeftTop,     -1, true, "LeftTop"},
    {ButtonId::LeftUpper,   -1, true, "LeftUpper"},
    {ButtonId::LeftMiddle,  -1, true, "LeftMiddle"},
    {ButtonId::LeftLower,   -1, true, "LeftLower"},
    {ButtonId::LeftBottom,  -1, true, "LeftBottom"},
    {ButtonId::RightTop,    -1, true, "RightTop"},
    {ButtonId::RightUpper,  -1, true, "RightUpper"},
    {ButtonId::RightMiddle, -1, true, "RightMiddle"},
    {ButtonId::RightLower,  -1, true, "RightLower"},
    {ButtonId::RightBottom, -1, true, "RightBottom"},
};

/// @brief Two UI-space framebuffers used for drawing and presentation.
/// @details New graphics are drawn into @c fb_back while @c fb_front remains
/// the current displayed image. Presenting a frame swaps the pointers.
static uint8_t fb_a[UI_FB_SIZE];
static uint8_t fb_b[UI_FB_SIZE];

static uint8_t* fb_front = fb_a;
static uint8_t* fb_back  = fb_b;

/// @brief Two scanout buffers that hold the exact DMA/PIO waveform for a frame.
/// @details These are different from the framebuffers above. A framebuffer is a
/// compact 1-bit image in UI space. A raster buffer is a packed stream of pin
/// states for VID, VCLK, HS and VS ready for DMA to feed the PIO state machine.
static uint32_t raster_a[RASTER_WORDS];
static uint32_t raster_b[RASTER_WORDS];

static uint32_t* raster_front = raster_a;
static uint32_t* raster_back  = raster_b;
static uint32_t* raster_pending = nullptr;

static const void* dma_read_addr = raster_a;

static int dma_chan_data;
static int dma_chan_ctrl;
static bool dma_running = false;
static ButtonState button_states[BUTTON_COUNT];
static uint8_t life_a[LIFE_CELL_COUNT];
static uint8_t life_b[LIFE_CELL_COUNT];
static uint8_t* life_front = life_a;
static uint8_t* life_back = life_b;
static uint32_t life_hash_ring[LIFE_HASH_HISTORY];
static size_t life_hash_index = 0;
static size_t life_hash_count = 0;

/// @brief DMA interrupt handler used to switch scanout buffers safely.
/// @details The data DMA channel streams one whole frame into the PIO TX FIFO.
/// A second control DMA channel then reloads the data channel's read address so
/// the same frame repeats forever.
///
/// This interrupt runs at that frame boundary. If a new raster has been queued,
/// it becomes the next frame without rewriting the live DMA source in place.
static void dma_ctrl_irq_handler()
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

/// @brief One fixed-width 5x7 font glyph stored as five columns.
/// @details The least-significant bit is the top pixel of each column.

struct Glyph5x7 {
    char c;
    uint8_t col[5];
};

static constexpr Glyph5x7 FONT_5X7[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x00,0x60,0x60,0x00}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'/', {0x40,0x20,0x10,0x08,0x04}},

    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x62,0x51,0x49,0x49,0x46}},
    {'3', {0x22,0x41,0x49,0x49,0x36}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x2F,0x49,0x49,0x49,0x31}},
    {'6', {0x3E,0x49,0x49,0x49,0x32}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x26,0x49,0x49,0x49,0x3E}},

    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x3E,0x41,0x51,0x21,0x5E}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}},
    {'V', {0x1F,0x20,0x40,0x20,0x1F}},
    {'W', {0x7F,0x20,0x18,0x20,0x7F}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x03,0x04,0x78,0x04,0x03}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}},
};

/// @brief Returns a glyph definition for the requested character.
/// @details Lowercase letters are folded to uppercase. Unsupported characters
/// fall back to a blank glyph so the caller never has to handle errors.
static const Glyph5x7* font_lookup(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }

    for (const auto& g : FONT_5X7) {
        if (g.c == c) {
            return &g;
        }
    }

    return &FONT_5X7[0];
}

// -----------------------------------------------------------------------------
// PIO / DMA setup
// -----------------------------------------------------------------------------

/// @brief Configures the PIO state machine that outputs the 4-bit raster stream.
/// @details The PIO program itself is intentionally tiny. The CPU does the hard
/// work by precomputing every output state in a raster buffer first.
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
    pio_sm_set_clkdiv(pio, sm, PANEL.clkdiv);
    pio_sm_set_enabled(pio, sm, true);
}

/// @brief Starts the repeating two-channel DMA scanout loop.
/// @details The data DMA channel pushes raster words into the PIO TX FIFO.
/// When that transfer finishes, the control DMA channel reloads the data
/// channel's read pointer so the same frame immediately starts again.
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

// -----------------------------------------------------------------------------
// UI framebuffer helpers
// -----------------------------------------------------------------------------

/// @brief Fills the whole UI framebuffer with black or white.
static inline void fb_clear(uint8_t* fb, bool on)
{
    memset(fb, on ? 0xFF : 0x00, UI_FB_SIZE);
}

/// @brief Sets one pixel in the logical portrait UI framebuffer.
/// @details This function knows only about UI coordinates. It does not know how
/// the physical panel is rotated or scanned.
static inline void fb_set_pixel(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return;
    }

    uint8_t& byte = fb[y * UI_STRIDE + (x >> 3)];
    uint8_t mask = 0x80u >> (x & 7);

    if (on) {
        byte |= mask;
    } else {
        byte &= static_cast<uint8_t>(~mask);
    }
}

/// @brief Reads one pixel from the logical portrait UI framebuffer.
static inline bool fb_get_pixel(const uint8_t* fb, int x, int y)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return false;
    }

    const uint8_t byte = fb[y * UI_STRIDE + (x >> 3)];
    const uint8_t mask = 0x80u >> (x & 7);
    return (byte & mask) != 0;
}

// -----------------------------------------------------------------------------
// Logical portrait-space drawing helpers
// -----------------------------------------------------------------------------

/// @brief Constrains a value to the supplied inclusive range.
static inline int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

/// @brief Draws a horizontal line in UI space.
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

/// @brief Draws a vertical line in UI space.
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

/// @brief Draws an outline rectangle in UI space.
static void fb_draw_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;

    fb_draw_hline(fb, x, x + w - 1, y, on);
    fb_draw_hline(fb, x, x + w - 1, y + h - 1, on);
    fb_draw_vline(fb, x, y, y + h - 1, on);
    fb_draw_vline(fb, x + w - 1, y, y + h - 1, on);
}

/// @brief Draws a filled rectangle in UI space.
static void fb_fill_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; ++yy) {
        fb_draw_hline(fb, x, x + w - 1, yy, on);
    }
}

/// @brief Draws a simple diagonal test line.
static void fb_draw_diag(uint8_t* fb, bool on)
{
    const int limit = std::min(UI_WIDTH, UI_HEIGHT);
    for (int i = 0; i < limit; ++i) {
        fb_set_pixel(fb, i, i, on);
    }
}

/// @brief Draws a general line using Bresenham's algorithm.
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

/// @brief Draws one 5x7 character at the requested scale.
static void fb_draw_char(uint8_t* fb, int x, int y, char c, bool on, int scale = 1)
{
    if (scale < 1) scale = 1;

    const Glyph5x7* g = font_lookup(c);

    for (int col = 0; col < 5; ++col) {
        const uint8_t bits = g->col[col];

        for (int row = 0; row < 7; ++row) {
            const bool pixel_on = ((bits >> row) & 0x01u) != 0;
            if (!pixel_on) continue;

            for (int sx = 0; sx < scale; ++sx) {
                for (int sy = 0; sy < scale; ++sy) {
                    fb_set_pixel(fb, x + col * scale + sx, y + row * scale + sy, on);
                }
            }
        }
    }
}

/// @brief Draws a null-terminated text string in UI space.
static void fb_draw_text(uint8_t* fb, int x, int y, const char* s, bool on, int scale = 1, int spacing = 1)
{
    int cursor_x = x;
    while (*s) {
        fb_draw_char(fb, cursor_x, y, *s, on, scale);
        cursor_x += (5 * scale) + spacing;
        ++s;
    }
}

// -----------------------------------------------------------------------------
// Input skeleton
// -----------------------------------------------------------------------------

/// @brief Converts a raw GPIO level into a logical "pressed" state.
static bool button_level_is_pressed(bool raw_level, const ButtonConfig& button)
{
    return button.active_low ? !raw_level : raw_level;
}

/// @brief Configures any keypad GPIOs that have been assigned real pins.
/// @details Buttons left at pin `-1` are skipped so the firmware can run
/// before the keypad hardware is connected.
static void buttons_init()
{
    const absolute_time_t now = get_absolute_time();

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        button_states[i] = {
            .raw_level = false,
            .stable_pressed = false,
            .last_change_time = now,
        };

        const ButtonConfig& button = BUTTONS[i];
        if (button.pin < 0) {
            continue;
        }

        gpio_init(static_cast<uint>(button.pin));
        gpio_set_dir(static_cast<uint>(button.pin), GPIO_IN);

        if (button.active_low) {
            gpio_pull_up(static_cast<uint>(button.pin));
        } else {
            gpio_pull_down(static_cast<uint>(button.pin));
        }

        const bool raw_level = gpio_get(static_cast<uint>(button.pin));
        button_states[i].raw_level = raw_level;
        button_states[i].stable_pressed = button_level_is_pressed(raw_level, button);
    }
}

/// @brief Polls one button and returns a debounced edge event if one occurred.
static ButtonEvent poll_button(ButtonState& state, const ButtonConfig& button)
{
    if (button.pin < 0) {
        return {button.id, ButtonEventType::None};
    }

    const bool raw_level = gpio_get(static_cast<uint>(button.pin));
    const absolute_time_t now = get_absolute_time();

    if (raw_level != state.raw_level) {
        state.raw_level = raw_level;
        state.last_change_time = now;
        return {button.id, ButtonEventType::None};
    }

    if (absolute_time_diff_us(state.last_change_time, now) < (BUTTON_DEBOUNCE_MS * 1000)) {
        return {button.id, ButtonEventType::None};
    }

    const bool pressed = button_level_is_pressed(raw_level, button);
    if (pressed == state.stable_pressed) {
        return {button.id, ButtonEventType::None};
    }

    state.stable_pressed = pressed;
    return {
        button.id,
        pressed ? ButtonEventType::Pressed : ButtonEventType::Released
    };
}

/// @brief Polls every configured button and returns the first edge event found.
/// @details This is enough for early bring-up. It can later be replaced by an
/// event queue if multiple button edges need to be captured per loop.
static ButtonEvent poll_buttons()
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        ButtonEvent event = poll_button(button_states[i], BUTTONS[i]);
        if (event.type != ButtonEventType::None) {
            return event;
        }
    }

    return {ButtonId::LeftTop, ButtonEventType::None};
}

/// @brief Temporary placeholder for future UI input handling.
/// @details The current firmware still flips between demo screens on a timer.
/// This function is where real menu navigation should be connected later.
static void handle_button_event(const ButtonEvent& event)
{
    if (event.type == ButtonEventType::None) {
        return;
    }

    for (const ButtonConfig& button : BUTTONS) {
        if (button.id == event.id) {
            const char* edge = (event.type == ButtonEventType::Pressed) ? "pressed" : "released";
            printf("Button %s %s\n", button.name, edge);
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// Game of Life screensaver
// -----------------------------------------------------------------------------

/// @brief Returns the linear array index for one Life cell.
static inline int life_index(int x, int y)
{
    return y * LIFE_WIDTH + x;
}

/// @brief Clears a Life simulation buffer.
static void life_clear(uint8_t* grid)
{
    memset(grid, 0, LIFE_CELL_COUNT);
}

/// @brief Seeds the Life simulation with a random starting pattern.
/// @details A moderate fill level gives the simulation enough activity to keep
/// the whole display moving without immediately becoming solid noise. This is
/// also used to restart the screensaver after it becomes too stable.
static void life_seed_random(uint8_t* grid)
{
    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        grid[i] = (rand() % 100) < 28 ? 1u : 0u;
    }
}

/// @brief Produces a simple hash for one Life grid.
/// @details This is used to spot repeating oscillator patterns without storing
/// whole generations for comparison.
static uint32_t life_hash_grid(const uint8_t* grid)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        hash ^= grid[i];
        hash *= 16777619u;
    }

    return hash;
}

/// @brief Clears the rolling history used for oscillator detection.
/// @details Call this after reseeding so old pattern hashes do not immediately
/// trigger another timeout.
static void life_reset_hash_history()
{
    memset(life_hash_ring, 0, sizeof(life_hash_ring));
    life_hash_index = 0;
    life_hash_count = 0;
}

/// @brief Records a new grid hash and reports whether it recently appeared.
/// @details Matching a recent hash means the Life field has entered a repeating
/// cycle, even if it is not identical to the immediately previous frame.
static bool life_record_hash_and_check_repeat(uint32_t hash)
{
    bool repeated = false;

    for (size_t i = 0; i < life_hash_count; ++i) {
        if (life_hash_ring[i] == hash) {
            repeated = true;
            break;
        }
    }

    life_hash_ring[life_hash_index] = hash;
    life_hash_index = (life_hash_index + 1) % LIFE_HASH_HISTORY;

    if (life_hash_count < LIFE_HASH_HISTORY) {
        ++life_hash_count;
    }

    return repeated;
}

/// @brief Counts the live neighbours around one Life cell.
/// @details The simulation wraps at the edges, so the left and right sides
/// connect together, and the top and bottom sides connect together.
static uint8_t life_count_neighbors(const uint8_t* grid, int x, int y)
{
    uint8_t count = 0;

    for (int yy = y - 1; yy <= y + 1; ++yy) {
        for (int xx = x - 1; xx <= x + 1; ++xx) {
            if (xx == x && yy == y) {
                continue;
            }

            const int wrapped_x = (xx + LIFE_WIDTH) % LIFE_WIDTH;
            const int wrapped_y = (yy + LIFE_HEIGHT) % LIFE_HEIGHT;

            count += grid[life_index(wrapped_x, wrapped_y)] != 0 ? 1u : 0u;
        }
    }

    return count;
}

/// @brief Advances the Life simulation by one generation.
/// @return True if the new generation differs from the previous one.
/// @details The screensaver uses this together with hash-based repeat detection
/// to reseed fields that become static or fall into short oscillator loops.
static bool life_step(const uint8_t* src, uint8_t* dst)
{
    int live_count = 0;
    bool changed = false;

    for (int y = 0; y < LIFE_HEIGHT; ++y) {
        for (int x = 0; x < LIFE_WIDTH; ++x) {
            const int index = life_index(x, y);
            const bool alive = src[index] != 0;
            const uint8_t neighbors = life_count_neighbors(src, x, y);
            const bool next_alive = alive
                ? (neighbors == 2 || neighbors == 3)
                : (neighbors == 3);

            dst[index] = next_alive ? 1u : 0u;
            live_count += next_alive ? 1 : 0;
            changed = changed || (dst[index] != src[index]);
        }
    }

    if (live_count == 0) {
        life_seed_random(dst);
        return true;
    }

    return changed;
}

/// @brief Swaps the front and back Life simulation grids.
static void life_swap()
{
    uint8_t* tmp = life_front;
    life_front = life_back;
    life_back = tmp;
}

/// @brief Renders the Life simulation into the UI framebuffer.
/// @details Each Life cell is drawn as a 2x2 block so the simulation fills the
/// entire portrait UI area while keeping the simulation grid smaller and faster.
/// This is deliberate: the screensaver is meant to create visible movement over
/// most of the panel rather than tiny single-pixel details.
static void draw_life_screen(uint8_t* fb, const uint8_t* grid)
{
    fb_clear(fb, false);

    for (int y = 0; y < LIFE_HEIGHT; ++y) {
        for (int x = 0; x < LIFE_WIDTH; ++x) {
            if (grid[life_index(x, y)] == 0) {
                continue;
            }

            fb_fill_rect(fb, x * LIFE_SCALE, y * LIFE_SCALE, LIFE_SCALE, LIFE_SCALE, true);
        }
    }
}

// -----------------------------------------------------------------------------
// Raster builder
// -----------------------------------------------------------------------------

/// @brief Appends one 4-bit output state to a packed raster buffer.
/// @details Each nibble contains one simultaneous state for VID, VCLK, HS and
/// VS. Eight of those nibbles are packed into one 32-bit word.
static inline void emit_step(uint32_t* buf, int& step_index, uint8_t nibble)
{
    const int word_index   = step_index / 8;
    const int nibble_index = step_index % 8;
    buf[word_index] |= (uint32_t)(nibble & 0x0F) << (nibble_index * 4);
    ++step_index;
}

/// @brief Emits one pixel time into the raster buffer.
/// @details Each pixel is stored as two output steps: clock low, then clock
/// high. This lets the CPU precompute the exact waveform and keeps the PIO
/// program extremely simple.
static inline void emit_pixel(uint32_t* buf, int& step_index, bool vid, bool hs, bool vs)
{
    uint8_t base = 0;
    if (vid) base |= BIT_VID;
    if (hs)  base |= BIT_HS;
    if (vs)  base |= BIT_VS;

    emit_step(buf, step_index, base);
    emit_step(buf, step_index, base | BIT_VCLK);
}

/// @brief Builds one fully blank scan line.
/// @details The line still includes the correct sync and blanking timing, but
/// the video data bit stays off.
static void build_blank_line(uint32_t* line_buf, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
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
/// @details This is the only place that knows how portrait UI coordinates map
/// to the panel's native electrical scan order and row offset.
static void build_fb_line(uint32_t* line_buf, const uint8_t* fb, int y, bool vs_high)
{
    memset(line_buf, 0, WORDS_PER_LINE * sizeof(uint32_t));
    int step = 0;
    const int max_ui_source_y = FB_HEIGHT - 1 + PANEL.native_row_offset;
    const int ui_x = clamp_int(max_ui_source_y - y, 0, UI_WIDTH - 1);

    for (int i = 0; i < PANEL.h_pre_blank; ++i) {
        emit_pixel(line_buf, step, false, true, vs_high);
    }

    for (int x = 0; x < FB_WIDTH; ++x) {
        const bool vid_on = (x < UI_HEIGHT) && (y <= max_ui_source_y)
            ? fb_get_pixel(fb, ui_x, x)
            : false;
        emit_pixel(line_buf, step, vid_on, true, vs_high);
    }

    for (int i = 0; i < PANEL.h_post_blank; ++i) {
        emit_pixel(line_buf, step, false, false, vs_high);
    }
}

/// @brief Rebuilds a complete scanout raster from a UI framebuffer.
/// @details The output buffer is ready for DMA to stream directly into the PIO
/// state machine.
static void rebuild_raster_from_fb(const uint8_t* fb, uint32_t* raster)
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

// -----------------------------------------------------------------------------
// Swap / present
// -----------------------------------------------------------------------------

/// @brief Swaps the UI front and back framebuffer pointers.
static void fb_swap()
{
    uint8_t* tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;
}

/// @brief Swaps the front and back raster buffer pointers.
static void raster_swap()
{
    uint32_t* tmp = raster_front;
    raster_front = raster_back;
    raster_back = tmp;
}

/// @brief Presents the current backbuffer.
/// @details This function swaps the UI buffers, converts the new front buffer
/// into the inactive scanout buffer, and queues that scanout buffer for the
/// next safe frame-boundary handoff.
static void present_backbuffer()
{
    fb_swap();
    rebuild_raster_from_fb(fb_front, raster_back);

    if (!dma_running) {
        raster_swap();
        return;
    }

    raster_pending = raster_back;
}

// -----------------------------------------------------------------------------
// Demo screens in PORTRAIT logical coordinates
// -----------------------------------------------------------------------------

/// @brief Draws a simple geometry test pattern.
/// @details This is useful when checking orientation, clipping and gross timing
/// issues on the physical display.
static void draw_demo_screen(uint8_t* fb)
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

/// @brief Draws a mock menu screen for bring-up and layout testing.
static void draw_dummy_menu_screen(uint8_t* fb)
{
    fb_clear(fb, false);

    // outer frame
    fb_draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    fb_draw_rect(fb, 6, 6, UI_WIDTH - 12, UI_HEIGHT - 12, true);

    // title bar
    fb_fill_rect(fb, 8, 8, UI_WIDTH - 16, 18, true);
    fb_draw_text(fb, 14, 14, "MERLIN CCU", false, 1, 1);

    // softkey markers and labels
    const char* left_labels[5] = {
        "LIGHTS",
        "HEAT",
        "GARAGE",
        "MEDIA",
        "ALARM"
    };

    const char* right_labels[5] = {
        "STATUS",
        "CAMERAS",
        "ENERGY",
        "SCENES",
        "SETUP"
    };

    for (int i = 0; i < 5; ++i) {
        const int y = 42 + i * 42;

        // left side
        fb_fill_rect(fb, 8, y + 4, 8, 10, true);
        fb_draw_rect(fb, 22, y, 82, 18, true);
        fb_draw_text(fb, 28, y + 5, left_labels[i], true, 1, 1);

        // right side
        fb_fill_rect(fb, UI_WIDTH - 16, y + 4, 8, 10, true);
        fb_draw_rect(fb, UI_WIDTH - 104, y, 82, 18, true);
        fb_draw_text(fb, UI_WIDTH - 98, y + 5, right_labels[i], true, 1, 1);
    }

    // central status panel
    fb_draw_rect(fb, 72, 54, 108, 148, true);

    fb_draw_text(fb, 96, 66, "HOME", true, 2, 2);
    fb_draw_text(fb, 84, 100, "TEMP  19", true, 1, 1);
    fb_draw_text(fb, 84, 116, "DOOR  SHUT", true, 1, 1);
    fb_draw_text(fb, 84, 132, "ALARM OFF", true, 1, 1);
    fb_draw_text(fb, 84, 148, "WIFI  GOOD", true, 1, 1);

    // bottom status bar
    fb_fill_rect(fb, 8, UI_HEIGHT - 20, UI_WIDTH - 16, 10, true);
    fb_draw_text(fb, 14, UI_HEIGHT - 18, "IDLE 00:42", false, 1, 1);
    fb_draw_text(fb, UI_WIDTH - 54, UI_HEIGHT - 18, "HA OK", false, 1, 1);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

/// @brief Firmware entry point.
/// @details The startup sequence is:
/// - draw an initial UI frame into the backbuffer
/// - convert that frame into the first scanout raster
/// - prepare any configured keypad GPIOs
/// - start PIO and DMA scanout
/// - keep drawing future frames into the backbuffer
int main()
{
    stdio_init_all();
    sleep_ms(2000);
    srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));

    printf("Dummy menu demo. clkdiv=%.2f row_offset=%d\n",
           PANEL.clkdiv, PANEL.native_row_offset);

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);
    const ScreenMode mode = ScreenMode::LifeScreensaver;
    absolute_time_t next_demo_flip = make_timeout_time_ms(3000);
    uint32_t life_frame_counter = 0;
    uint32_t life_stable_frames = 0;
    uint32_t life_repeat_frames = 0;
    absolute_time_t next_life_stats = make_timeout_time_ms(1000);

    if (mode == ScreenMode::LifeScreensaver) {
        life_clear(life_front);
        life_seed_random(life_front);
        life_reset_hash_history();
        (void)life_record_hash_and_check_repeat(life_hash_grid(life_front));
        draw_life_screen(fb_back, life_front);
    } else {
        draw_demo_screen(fb_back);
    }
    present_backbuffer();
    buttons_init();

    el320_raster_program_init(pio, sm, offset, PIN_BASE);
    start_dma(pio, sm);

    bool show_menu = false;

    while (true) {
        handle_button_event(poll_buttons());

        if (mode == ScreenMode::LifeScreensaver) {
            const absolute_time_t step_start = get_absolute_time();
            const bool life_changed = life_step(life_front, life_back);
            const int64_t sim_us = absolute_time_diff_us(step_start, get_absolute_time());
            const bool life_repeated = life_record_hash_and_check_repeat(life_hash_grid(life_back));

            if (life_changed) {
                life_stable_frames = 0;
            } else {
                ++life_stable_frames;
            }

            if (life_repeated) {
                ++life_repeat_frames;
            } else {
                life_repeat_frames = 0;
            }

            if (life_stable_frames >= LIFE_STABLE_RESEED_FRAMES) {
                life_seed_random(life_back);
                life_stable_frames = 0;
                life_repeat_frames = 0;
                life_reset_hash_history();
                (void)life_record_hash_and_check_repeat(life_hash_grid(life_back));
                printf("Life reseeded after stable timeout\n");
            } else if (life_repeat_frames >= LIFE_REPEAT_RESEED_FRAMES) {
                life_seed_random(life_back);
                life_stable_frames = 0;
                life_repeat_frames = 0;
                life_reset_hash_history();
                (void)life_record_hash_and_check_repeat(life_hash_grid(life_back));
                printf("Life reseeded after repeat-cycle timeout\n");
            }

            const absolute_time_t draw_start = get_absolute_time();
            draw_life_screen(fb_back, life_back);
            const int64_t draw_us = absolute_time_diff_us(draw_start, get_absolute_time());

            const absolute_time_t present_start = get_absolute_time();
            present_backbuffer();
            const int64_t present_us = absolute_time_diff_us(present_start, get_absolute_time());

            life_swap();
            ++life_frame_counter;

            if (absolute_time_diff_us(get_absolute_time(), next_life_stats) <= 0) {
                printf("Life fps=%lu sim=%lldus draw=%lldus present=%lldus\n",
                       static_cast<unsigned long>(life_frame_counter),
                       sim_us,
                       draw_us,
                       present_us);
                life_frame_counter = 0;
                next_life_stats = make_timeout_time_ms(1000);
            }

            sleep_ms(75);
        } else {
            sleep_ms(20);

            if (absolute_time_diff_us(get_absolute_time(), next_demo_flip) <= 0) {
                if (show_menu) {
                    draw_dummy_menu_screen(fb_back);
                    printf("Presenting dummy menu screen\n");
                } else {
                    draw_demo_screen(fb_back);
                    printf("Presenting demo screen\n");
                }

                present_backbuffer();
                show_menu = !show_menu;
                next_demo_flip = make_timeout_time_ms(3000);
            }
        }
    }

    return 0;
}
