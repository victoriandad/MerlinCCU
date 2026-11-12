#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Settings root page and its subpages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, softkey brackets, paged navigation arrows) stay there in
/// `screens_shared.h`; this file owns only the Settings-page-specific
/// formatting and layout. Settings subpages whose values all live on the
/// surrounding softkeys have no body renderer; `screens::draw_menu_screen()`
/// lists them as key pages with no body.
namespace settings_screens
{

/// @brief Draws the top-level settings routing page.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws only the paging arrows of the Time Zone option list.
void draw_time_zone_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws only active screen-saver editing UI.
void draw_screen_saver_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the keypad-debug diagnostics page.
void draw_keypad_debug_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the 2x2 ordered-dither greyscale test card.
void draw_greyscale_test_card(uint8_t* fb, const ConsoleState& console_state);

} // namespace settings_screens
