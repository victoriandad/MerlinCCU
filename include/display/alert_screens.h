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

/// @brief Returns the highest scroll position for an alert's detail text.
/// @details Computed from the wrapped line count at the page's font and width,
/// so it is the one definition shared by the renderer and the cursor-key
/// handling in console_controller.cpp: scrolling stops once the last line is
/// in view. Pure layout arithmetic; draws nothing.
uint8_t detail_max_scroll(const ActiveAlert& alert);

/// @brief Draws the History page body (#203): empty, or the CLEAR ALL
/// question. The entries themselves are the softkeys.
void draw_alert_history_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the open past alert: what it was, when it was raised and
/// ended, how it ended, then its detail, with line-based scrolling.
void draw_alert_history_detail_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Highest scroll position for the open past alert's text; 0 when
/// none is open. Shared with the cursor handling, like detail_max_scroll().
uint8_t history_detail_max_scroll(const ConsoleState& console_state);

} // namespace alert_screens
