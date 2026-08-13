#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Alert list and detail pages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (paged
/// navigation arrows, centred text) stay there in `screens_shared.h`; this
/// file owns only the Alert-page-specific formatting and layout.
namespace alert_screens
{

/// @brief Draws compact status lines for the alert-list page.
void draw_alert_list_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the selected alert detail text with line-based scrolling.
void draw_alert_detail_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace alert_screens
