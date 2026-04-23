#include "screensaver_starfield.h"

#include <array>
#include <cstdlib>

#include "framebuffer.h"
#include "panel_config.h"
#include "pico/stdlib.h"

namespace screensaver_starfield
{

namespace
{

struct Star
{
    int x;
    int y;
    int z;
};

constexpr size_t kStarCount = 48;
constexpr int kDepthMax = 96;
constexpr int kDepthMin = 8;
constexpr int kCenterX = kUiWidth / 2;
constexpr int kCenterY = kUiHeight / 2;

std::array<Star, kStarCount> g_stars = {};

/// @brief Returns a random signed offset around the screen center.
int random_offset(int radius)
{
    return (std::rand() % (radius * 2 + 1)) - radius;
}

/// @brief Respawns one star back near the center with a fresh depth.
void respawn_star(Star& star)
{
    star.x = random_offset(kCenterX);
    star.y = random_offset(kCenterY);
    star.z = kDepthMin + (std::rand() % (kDepthMax - kDepthMin + 1));
}

} // namespace

/// @brief Initializes the starfield screensaver state.
void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    for (Star& star : g_stars)
    {
        respawn_star(star);
    }
}

/// @brief Advances the starfield screensaver and renders one frame.
void step_and_render(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    for (Star& star : g_stars)
    {
        star.z -= 2;
        if (star.z < kDepthMin)
        {
            respawn_star(star);
        }

        const int screen_x = kCenterX + ((star.x * kDepthMax) / star.z);
        const int screen_y = kCenterY + ((star.y * kDepthMax) / star.z);
        const int tail_x = kCenterX + ((star.x * kDepthMax) / (star.z + 6));
        const int tail_y = kCenterY + ((star.y * kDepthMax) / (star.z + 6));

        if (screen_x < 0 || screen_x >= kUiWidth || screen_y < 0 || screen_y >= kUiHeight)
        {
            respawn_star(star);
            continue;
        }

        framebuffer::set_pixel(fb, screen_x, screen_y, true);
        framebuffer::draw_line(fb, tail_x, tail_y, screen_x, screen_y, true);
    }
}

} // namespace screensaver_starfield
