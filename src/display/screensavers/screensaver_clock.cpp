#include "screensaver_clock.h"

#include <cstdio>
#include <cstring>

#include "fonts.h"
#include "framebuffer.h"
#include "panel_config.h"

namespace screensaver_clock
{

namespace
{

constexpr fonts::FontFace kClockFont = fonts::FontFace::Font8x14;
constexpr int kClockStepX = 1;
constexpr int kClockStepY = 1;

int g_clock_x = 0;
int g_clock_y = 0;
int g_clock_dx = kClockStepX;
int g_clock_dy = kClockStepY;

/// @brief Returns the rendered width of one clock string.
int clock_text_width(const char* text)
{
    return framebuffer::measure_text(text, kClockFont, 1);
}

/// @brief Returns the rendered height of the clock font.
int clock_text_height()
{
    return framebuffer::font_height(kClockFont);
}

} // namespace

/// @brief Initializes the animated clock screensaver state.
void init()
{
    const int text_height = clock_text_height();
    g_clock_x = (kUiWidth - clock_text_width("00:00")) / 2;
    g_clock_y = (kUiHeight - text_height) / 2;
    g_clock_dx = kClockStepX;
    g_clock_dy = kClockStepY;
}

/// @brief Advances the clock screensaver and renders one frame.
void step_and_render(uint8_t* fb, const TimeStatus& time_status)
{
    char clock_text[8] = {};
    std::snprintf(clock_text, sizeof(clock_text), "%s",
                  (time_status.synced && time_status.time_text[0] != '\0')
                      ? time_status.time_text.data()
                      : "--:--");

    const int text_width = clock_text_width(clock_text);
    const int text_height = clock_text_height();
    const int max_x = kUiWidth - text_width;
    const int max_y = kUiHeight - text_height;

    framebuffer::clear(fb, false);
    framebuffer::draw_text(fb, g_clock_x, g_clock_y, clock_text, true, kClockFont, 1);

    g_clock_x += g_clock_dx;
    g_clock_y += g_clock_dy;

    if (g_clock_x <= 0 || g_clock_x >= max_x)
    {
        g_clock_dx = -g_clock_dx;
        g_clock_x += g_clock_dx;
    }

    if (g_clock_y <= 0 || g_clock_y >= max_y)
    {
        g_clock_dy = -g_clock_dy;
        g_clock_y += g_clock_dy;
    }
}

} // namespace screensaver_clock
