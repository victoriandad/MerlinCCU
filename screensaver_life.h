#pragma once

#include <cstdint>

/// @brief Timing breakdown for one rendered Life frame.
struct LifeFrameStats
{
    int64_t sim_us;
    int64_t draw_us;
    int64_t present_us;
};

namespace screensaver_life
{

/// @brief Seeds the Conway's Game of Life field used by the screensaver.
void init();

/// @brief Advances one Life generation and renders it into the target framebuffer.
/// @param fb Target UI framebuffer.
/// @param stats Receives rough timing information for simulation, drawing and
/// later presentation.
void step_and_render(uint8_t* fb, LifeFrameStats& stats);

} // namespace screensaver_life
