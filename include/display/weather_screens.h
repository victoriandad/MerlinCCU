#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the live Weather page and its forecast periods (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, centred text, label wrapping) stay there; this file owns only the
/// Weather-page-specific formatting, forecast slicing, and layout.
namespace weather_screens
{

/// @brief Draws the live weather page reached directly from Home.
void draw_weather_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace weather_screens
