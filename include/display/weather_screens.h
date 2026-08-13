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
/// @details `now_ms` is the caller's boot-uptime sample (`to_ms_since_boot`)
/// used for the source footer's data-freshness text -- passed in rather than
/// read here so this file has no Pico SDK dependency (issue #71).
void draw_weather_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

} // namespace weather_screens
