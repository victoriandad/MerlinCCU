#pragma once

#include <cstdint>

#include "console_model.h"

namespace screensaver_clock
{

/// @brief Initializes the animated clock screensaver state.
void init();

/// @brief Advances the clock screensaver and renders one frame.
void step_and_render(uint8_t* fb, const TimeStatus& time_status);

} // namespace screensaver_clock
