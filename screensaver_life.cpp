#include "screensaver_life.h"

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>

#include "pico/stdlib.h"

#include "framebuffer.h"
#include "panel_config.h"

namespace screensaver_life {

namespace {

constexpr int LIFE_SCALE = 2;
constexpr int LIFE_WIDTH = UI_WIDTH / LIFE_SCALE;
constexpr int LIFE_HEIGHT = UI_HEIGHT / LIFE_SCALE;
constexpr int LIFE_CELL_COUNT = LIFE_WIDTH * LIFE_HEIGHT;
constexpr uint32_t LIFE_STABLE_RESEED_FRAMES = 200;
constexpr size_t LIFE_HASH_HISTORY = 8;
constexpr uint32_t LIFE_REPEAT_RESEED_FRAMES = 100;

uint8_t life_a[LIFE_CELL_COUNT];
uint8_t life_b[LIFE_CELL_COUNT];
uint8_t* life_front = life_a;
uint8_t* life_back = life_b;
uint32_t life_hash_ring[LIFE_HASH_HISTORY];
size_t life_hash_index = 0;
size_t life_hash_count = 0;
uint32_t life_stable_frames = 0;
uint32_t life_repeat_frames = 0;

inline int life_index(int x, int y)
{
    return y * LIFE_WIDTH + x;
}

void life_clear(uint8_t* grid)
{
    std::memset(grid, 0, LIFE_CELL_COUNT);
}

void life_seed_random(uint8_t* grid)
{
    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        grid[i] = (std::rand() % 100) < 28 ? 1u : 0u;
    }
}

uint32_t life_hash_grid(const uint8_t* grid)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < LIFE_CELL_COUNT; ++i) {
        hash ^= grid[i];
        hash *= 16777619u;
    }

    return hash;
}

void life_reset_hash_history()
{
    std::memset(life_hash_ring, 0, sizeof(life_hash_ring));
    life_hash_index = 0;
    life_hash_count = 0;
}

bool life_record_hash_and_check_repeat(uint32_t hash)
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

uint8_t life_count_neighbors(const uint8_t* grid, int x, int y)
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

bool life_step(const uint8_t* src, uint8_t* dst)
{
    int live_count = 0;
    bool changed = false;

    for (int y = 0; y < LIFE_HEIGHT; ++y) {
        for (int x = 0; x < LIFE_WIDTH; ++x) {
            const int index = life_index(x, y);
            const bool alive = src[index] != 0;
            const uint8_t neighbors = life_count_neighbors(src, x, y);
            const bool next_alive = alive ? (neighbors == 2 || neighbors == 3) : (neighbors == 3);

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

void life_swap()
{
    uint8_t* tmp = life_front;
    life_front = life_back;
    life_back = tmp;
}

void draw_life_screen(uint8_t* fb, const uint8_t* grid)
{
    framebuffer::clear(fb, false);

    for (int y = 0; y < LIFE_HEIGHT; ++y) {
        for (int x = 0; x < LIFE_WIDTH; ++x) {
            if (grid[life_index(x, y)] == 0) {
                continue;
            }

            framebuffer::fill_rect(fb, x * LIFE_SCALE, y * LIFE_SCALE, LIFE_SCALE, LIFE_SCALE, true);
        }
    }
}

}  // namespace

void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));
    life_clear(life_front);
    life_seed_random(life_front);
    life_reset_hash_history();
    (void)life_record_hash_and_check_repeat(life_hash_grid(life_front));
    life_stable_frames = 0;
    life_repeat_frames = 0;
}

void step_and_render(uint8_t* fb, LifeFrameStats& stats)
{
    const absolute_time_t step_start = get_absolute_time();
    const bool life_changed = life_step(life_front, life_back);
    stats.sim_us = absolute_time_diff_us(step_start, get_absolute_time());

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
        std::printf("Life reseeded after stable timeout\n");
    } else if (life_repeat_frames >= LIFE_REPEAT_RESEED_FRAMES) {
        life_seed_random(life_back);
        life_stable_frames = 0;
        life_repeat_frames = 0;
        life_reset_hash_history();
        (void)life_record_hash_and_check_repeat(life_hash_grid(life_back));
        std::printf("Life reseeded after repeat-cycle timeout\n");
    }

    const absolute_time_t draw_start = get_absolute_time();
    draw_life_screen(fb, life_back);
    stats.draw_us = absolute_time_diff_us(draw_start, get_absolute_time());

    life_swap();
}

}  // namespace screensaver_life
