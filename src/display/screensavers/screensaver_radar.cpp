#include "screensaver_radar.h"

#include <array>
#include <cstdlib>

#include "framebuffer.h"
#include "panel_config.h"
#include "pico/stdlib.h"

namespace screensaver_radar
{

namespace
{

struct SweepVector
{
    int x;
    int y;
};

struct RadarBlip
{
    int x;
    int y;
    int age;
};

constexpr int kCenterX = kUiWidth / 2;
constexpr int kCenterY = kUiHeight / 2;
constexpr int kRadius = (kUiWidth < kUiHeight ? kUiWidth : kUiHeight) / 2 - 10;
constexpr int kVectorScale = 1000;
constexpr int kSweepCount = 32;
constexpr int kTrailCount = 5;
constexpr int kBlipCount = 14;
constexpr int kBlipLifetime = 42;

constexpr std::array<SweepVector, kSweepCount> kSweepVectors = {{
    {1000, 0},   {981, 195},   {924, 383},   {831, 556},   {707, 707},   {556, 831},
    {383, 924},  {195, 981},   {0, 1000},    {-195, 981},  {-383, 924},  {-556, 831},
    {-707, 707}, {-831, 556},  {-924, 383},  {-981, 195},  {-1000, 0},   {-981, -195},
    {-924, -383}, {-831, -556}, {-707, -707}, {-556, -831}, {-383, -924}, {-195, -981},
    {0, -1000},  {195, -981},  {383, -924},  {556, -831},  {707, -707},  {831, -556},
    {924, -383}, {981, -195},
}};

std::array<RadarBlip, kBlipCount> g_blips = {};
int g_sweep_index = 0;

/// @brief Draws an integer midpoint circle with framebuffer clipping delegated to set_pixel.
void draw_circle(uint8_t* fb, int cx, int cy, int radius)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        framebuffer::set_pixel(fb, cx + x, cy + y, true);
        framebuffer::set_pixel(fb, cx + y, cy + x, true);
        framebuffer::set_pixel(fb, cx - y, cy + x, true);
        framebuffer::set_pixel(fb, cx - x, cy + y, true);
        framebuffer::set_pixel(fb, cx - x, cy - y, true);
        framebuffer::set_pixel(fb, cx - y, cy - x, true);
        framebuffer::set_pixel(fb, cx + y, cy - x, true);
        framebuffer::set_pixel(fb, cx + x, cy - y, true);

        ++y;
        if (err <= 0)
        {
            err += (2 * y) + 1;
        }
        if (err > 0)
        {
            --x;
            err -= (2 * x) + 1;
        }
    }
}

/// @brief Places a blip at a random range and bearing inside the radar circle.
void reset_blip(RadarBlip& blip)
{
    const SweepVector& vector = kSweepVectors[std::rand() % kSweepCount];
    const int range = 12 + (std::rand() % (kRadius - 12));
    blip.x = kCenterX + ((vector.x * range) / kVectorScale);
    blip.y = kCenterY + ((vector.y * range) / kVectorScale);
    blip.age = std::rand() % kBlipLifetime;
}

} // namespace

/// @brief Initializes the rotating radar sweep screensaver state.
void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    g_sweep_index = 0;
    for (RadarBlip& blip : g_blips)
    {
        reset_blip(blip);
    }
}

/// @brief Advances the radar sweep and renders one frame.
void step_and_render(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    draw_circle(fb, kCenterX, kCenterY, kRadius);
    draw_circle(fb, kCenterX, kCenterY, kRadius / 3);
    draw_circle(fb, kCenterX, kCenterY, (kRadius * 2) / 3);
    framebuffer::draw_hline(fb, kCenterX - kRadius, kCenterX + kRadius, kCenterY, true);
    framebuffer::draw_vline(fb, kCenterX, kCenterY - kRadius, kCenterY + kRadius, true);

    for (int trail = kTrailCount - 1; trail >= 0; --trail)
    {
        const int vector_index = (g_sweep_index + kSweepCount - trail) % kSweepCount;
        const SweepVector& vector = kSweepVectors[vector_index];
        const int end_x = kCenterX + ((vector.x * kRadius) / kVectorScale);
        const int end_y = kCenterY + ((vector.y * kRadius) / kVectorScale);
        framebuffer::draw_line(fb, kCenterX, kCenterY, end_x, end_y, true);
    }

    for (RadarBlip& blip : g_blips)
    {
        --blip.age;
        if (blip.age <= 0)
        {
            reset_blip(blip);
            blip.age = kBlipLifetime;
        }

        if ((blip.age % 4) != 0)
        {
            framebuffer::fill_rect(fb, blip.x - 1, blip.y - 1, 3, 3, true);
        }
    }

    g_sweep_index = (g_sweep_index + 1) % kSweepCount;
}

} // namespace screensaver_radar
