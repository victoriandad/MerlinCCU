#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Local Traffic (ADS-B) page (issue #74).
/// @details Split out the same way as the other page families (#45): shared
/// drawing primitives stay in `screens.cpp`, this file owns only the
/// air-traffic-specific table layout.
namespace air_traffic_screens
{

/// @brief Draws the compact nearby-aircraft table, or a status page when the
/// feature is disabled/unconfigured/has no data yet.
void draw_air_traffic_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace air_traffic_screens
