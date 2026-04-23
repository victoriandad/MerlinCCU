#pragma once

#include <cstdint>

namespace screensaver_radar
{

/// @brief Initializes the rotating radar sweep screensaver state.
void init();

/// @brief Advances the radar sweep and renders one frame.
void step_and_render(uint8_t* fb);

} // namespace screensaver_radar
