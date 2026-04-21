#include "screensaver_life.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "debug_logging.h"
#include "pico/stdlib.h"

#include "framebuffer.h"
#include "panel_config.h"

namespace screensaver_life
{

namespace
{

/// @brief Simulation geometry and reseed thresholds for the Life screensaver.
/// @details The automaton runs on a coarser grid than the panel so each generation stays
/// cheap on the Pico while still filling the display once cells are scaled back up.
constexpr int kLifeScale = 2;
constexpr int kLifeWidth = kUiWidth / kLifeScale;
constexpr int kLifeHeight = kUiHeight / kLifeScale;
constexpr int kLifeCellCount = kLifeWidth * kLifeHeight;
constexpr uint32_t kLifeStableReseedFrames = 200;
constexpr size_t kLifeHashHistorySize = 8;
constexpr uint32_t kLifeRepeatReseedFrames = 100;

uint8_t life_a[kLifeCellCount];
uint8_t life_b[kLifeCellCount];
uint8_t* life_front = life_a;
uint8_t* life_back = life_b;
uint32_t life_hash_ring[kLifeHashHistorySize];
size_t life_hash_index = 0;
size_t life_hash_count = 0;
uint32_t life_stable_frames = 0;
uint32_t life_repeat_frames = 0;

/// @brief Converts 2D Life coordinates into the flat backing-array index.
/// @details The simulation uses flat arrays instead of nested containers so each generation can
/// step and swap cheaply on the Pico.
inline int life_index(int x, int y)
{
    return y * kLifeWidth + x;
}

/// @brief Clears a Life grid to an all-dead state.
void life_clear(uint8_t* grid)
{
    std::memset(grid, 0, kLifeCellCount);
}

/// @brief Seeds a Life grid with a sparse random population.
void life_seed_random(uint8_t* grid)
{
    for (int i = 0; i < kLifeCellCount; ++i)
    {
        grid[i] = (std::rand() % 100) < 28 ? 1U : 0U;
    }
}

/// @brief Hashes the current Life grid so repeats can be detected cheaply.
uint32_t life_hash_grid(const uint8_t* grid)
{
    uint32_t hash = 2166136261U;

    for (int i = 0; i < kLifeCellCount; ++i)
    {
        hash ^= grid[i];
        hash *= 16777619U;
    }

    return hash;
}

/// @brief Clears the recent grid-hash history used for repeat detection.
void life_reset_hash_history()
{
    std::memset(life_hash_ring, 0, sizeof(life_hash_ring));
    life_hash_index = 0;
    life_hash_count = 0;
}

/// @brief Records one grid hash and returns whether it was seen recently.
bool life_record_hash_and_check_repeat(uint32_t hash)
{
    bool repeated = false;

    for (size_t i = 0; i < life_hash_count; ++i)
    {
        if (life_hash_ring[i] == hash)
        {
            repeated = true;
            break;
        }
    }

    life_hash_ring[life_hash_index] = hash;
    life_hash_index = (life_hash_index + 1) % kLifeHashHistorySize;
    if (life_hash_count < kLifeHashHistorySize)
    {
        ++life_hash_count;
    }

    return repeated;
}

uint8_t life_count_neighbors(const uint8_t* grid, int x, int y)
{
    uint8_t count = 0;

    for (int yy = y - 1; yy <= y + 1; ++yy)
    {
        for (int xx = x - 1; xx <= x + 1; ++xx)
        {
            if (xx == x && yy == y)
            {
                continue;
            }

            const int wrapped_x = (xx + kLifeWidth) % kLifeWidth;
            const int wrapped_y = (yy + kLifeHeight) % kLifeHeight;
                count += grid[life_index(wrapped_x, wrapped_y)] != 0 ? 1U : 0U;
        }
    }

    return count;
}

/// @brief Advances the Life automaton by one generation.
bool life_step(const uint8_t* src, uint8_t* dst)
{
    int live_count = 0;
    bool changed = false;

    for (int y = 0; y < kLifeHeight; ++y)
    {
        for (int x = 0; x < kLifeWidth; ++x)
        {
            const int index = life_index(x, y);
            const bool alive = src[index] != 0;
            const uint8_t neighbors = life_count_neighbors(src, x, y);
            const bool next_alive = alive ? (neighbors == 2 || neighbors == 3) : (neighbors == 3);

            dst[index] = next_alive ? 1U : 0U;
            live_count += next_alive ? 1 : 0;
            changed = changed || (dst[index] != src[index]);
        }
    }

    if (live_count == 0)
    {
        life_seed_random(dst);
        return true;
    }

    return changed;
}

/// @brief Swaps the front and back Life grids.
void life_swap()
{
    uint8_t* tmp = life_front;
    life_front = life_back;
    life_back = tmp;
}

/// @brief Renders one Life grid into the framebuffer.
void draw_life_screen(uint8_t* fb, const uint8_t* grid)
{
    framebuffer::clear(fb, false);

    for (int y = 0; y < kLifeHeight; ++y)
    {
        for (int x = 0; x < kLifeWidth; ++x)
        {
            if (grid[life_index(x, y)] == 0)
            {
                continue;
            }

            framebuffer::fill_rect(fb, x * kLifeScale, y * kLifeScale, kLifeScale, kLifeScale,
                                   true);
        }
    }
}

} // namespace

/// @brief Initializes the Life screensaver with a fresh random seed.
void init()
{
    // Seed from uptime so repeated power cycles do not always land on the same
    // opening pattern during bench demos.
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    life_clear(life_front);
    life_seed_random(life_front);
    life_reset_hash_history();
    (void)life_record_hash_and_check_repeat(life_hash_grid(life_front));
    life_stable_frames = 0;
    life_repeat_frames = 0;
}

/// @brief Advances the simulation, applies reseed logic, and renders one frame.
void step_and_render(uint8_t* fb, LifeFrameStats& stats)
{
    // Keep simulation timing separate from draw timing so scan performance and
    // automaton performance can be tuned independently.
    const absolute_time_t step_start = get_absolute_time();
    const bool life_changed = life_step(life_front, life_back);
    stats.sim_us = absolute_time_diff_us(step_start, get_absolute_time());

    // Stable and repeating patterns are treated differently so the screensaver
    // recovers both from static extinction and from short oscillator loops.
    const bool life_repeated = life_record_hash_and_check_repeat(life_hash_grid(life_back));

    if (life_changed)
    {
        life_stable_frames = 0;
    }
    else
    {
        ++life_stable_frames;
    }

    if (life_repeated)
    {
        ++life_repeat_frames;
    }
    else
    {
        life_repeat_frames = 0;
    }

    // Reseed before drawing so the user immediately sees a fresh pattern rather
    // than one extra stale frame after the timeout triggers.
    if (life_stable_frames >= kLifeStableReseedFrames)
    {
        life_seed_random(life_back);
        life_stable_frames = 0;
        life_repeat_frames = 0;
        life_reset_hash_history();
        (void)life_record_hash_and_check_repeat(life_hash_grid(life_back));
        PERIODIC_LOG("Life reseeded after stable timeout\n");
    }
    else if (life_repeat_frames >= kLifeRepeatReseedFrames)
    {
        life_seed_random(life_back);
        life_stable_frames = 0;
        life_repeat_frames = 0;
        life_reset_hash_history();
        (void)life_record_hash_and_check_repeat(life_hash_grid(life_back));
        PERIODIC_LOG("Life reseeded after repeat-cycle timeout\n");
    }

    // Render from the newly computed back grid, then swap ownership so the next
    // simulation step reads the frame that was just displayed.
    const absolute_time_t draw_start = get_absolute_time();
    draw_life_screen(fb, life_back);
    stats.draw_us = absolute_time_diff_us(draw_start, get_absolute_time());

    life_swap();
}

} // namespace screensaver_life
