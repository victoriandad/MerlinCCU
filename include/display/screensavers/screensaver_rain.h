#pragma once

#include <cstdint>

namespace screensaver_rain
{

/// @brief Initializes the diagonal rain screensaver state.
void init();

/// @brief Advances the rain screensaver and renders one frame.
void step_and_render(uint8_t* fb);

} // namespace screensaver_rain
