#include "screensaver_worms.h"

#include <array>
#include <cstdlib>

#include "framebuffer.h"
#include "panel_config.h"
#include "pico/stdlib.h"

namespace screensaver_worms
{

namespace
{

struct Point
{
    int x;
    int y;
};

struct Worm
{
    std::array<Point, 18> trail;
    int head;
    int dx;
    int dy;
};

constexpr int kWormCount = 8;
constexpr int kTrailLength = 18;

std::array<Worm, kWormCount> g_worms = {};

/// @brief Chooses a non-zero step vector so worms keep crawling every frame.
void choose_direction(Worm& worm)
{
    do
    {
        worm.dx = (std::rand() % 3) - 1;
        worm.dy = (std::rand() % 3) - 1;
    } while (worm.dx == 0 && worm.dy == 0);
}

/// @brief Resets one worm near a random point with its trail collapsed onto the head.
void reset_worm(Worm& worm)
{
    const Point start = {std::rand() % kUiWidth, std::rand() % kUiHeight};
    for (Point& point : worm.trail)
    {
        point = start;
    }
    worm.head = 0;
    choose_direction(worm);
}

} // namespace

/// @brief Initializes the wandering worms screensaver state.
void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    for (Worm& worm : g_worms)
    {
        reset_worm(worm);
    }
}

/// @brief Advances the worms screensaver and renders one frame.
void step_and_render(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    for (Worm& worm : g_worms)
    {
        if ((std::rand() % 8) == 0)
        {
            choose_direction(worm);
        }

        const Point& current = worm.trail[worm.head];
        Point next = {current.x + worm.dx, current.y + worm.dy};
        if (next.x < 0 || next.x >= kUiWidth || next.y < 0 || next.y >= kUiHeight)
        {
            choose_direction(worm);
            next.x = current.x + worm.dx;
            next.y = current.y + worm.dy;
        }

        next.x = (next.x + kUiWidth) % kUiWidth;
        next.y = (next.y + kUiHeight) % kUiHeight;
        worm.head = (worm.head + 1) % kTrailLength;
        worm.trail[worm.head] = next;

        for (int offset = 0; offset < kTrailLength - 1; ++offset)
        {
            const int index_a = (worm.head + kTrailLength - offset) % kTrailLength;
            const int index_b = (worm.head + kTrailLength - offset - 1) % kTrailLength;
            const Point& a = worm.trail[index_a];
            const Point& b = worm.trail[index_b];
            framebuffer::draw_line(fb, a.x, a.y, b.x, b.y, true);
        }
    }
}

} // namespace screensaver_worms
