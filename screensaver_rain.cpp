#include "screensaver_rain.h"

#include <array>
#include <cstdlib>

#include "framebuffer.h"
#include "panel_config.h"
#include "pico/stdlib.h"

namespace screensaver_rain
{

namespace
{

struct RainDrop
{
    int x;
    int y;
    int speed;
    int length;
};

constexpr int kDropCount = 72;
constexpr int kSlant = 4;

std::array<RainDrop, kDropCount> g_drops = {};

/// @brief Restarts one drop above the panel so rain enters from the top edge.
void reset_drop(RainDrop& drop)
{
    drop.x = std::rand() % kUiWidth;
    drop.y = -(std::rand() % kUiHeight);
    drop.speed = 4 + (std::rand() % 5);
    drop.length = 8 + (std::rand() % 18);
}

} // namespace

/// @brief Initializes the diagonal rain screensaver state.
void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    for (RainDrop& drop : g_drops)
    {
        reset_drop(drop);
        drop.y = std::rand() % kUiHeight;
    }
}

/// @brief Advances the rain screensaver and renders one frame.
void step_and_render(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    for (RainDrop& drop : g_drops)
    {
        drop.y += drop.speed;
        drop.x += 1;
        if (drop.y - drop.length > kUiHeight || drop.x >= kUiWidth + kSlant)
        {
            reset_drop(drop);
        }

        framebuffer::draw_line(fb, drop.x, drop.y, drop.x - kSlant, drop.y - drop.length, true);
    }
}

} // namespace screensaver_rain
