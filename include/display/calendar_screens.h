#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Calendar overview and event-detail pages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, centred text) stay there in `screens_shared.h`; this file owns only
/// the Calendar-page-specific formatting and layout.
namespace calendar_screens
{

/// @brief Draws the shared calendar overview selected from Home.
void draw_calendar_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws detailed data for the selected shared calendar event.
void draw_calendar_detail_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace calendar_screens
