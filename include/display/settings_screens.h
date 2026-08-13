#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Settings root page and its subpages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, softkey brackets, paged navigation arrows) stay there in
/// `screens_shared.h`; this file owns only the Settings-page-specific
/// formatting and layout. Most subpages leave the body intentionally blank
/// since their values live on the surrounding softkeys -- see each
/// function's own doc comment.
namespace settings_screens
{

/// @brief Draws the top-level settings routing page.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the device identity settings body blank.
void draw_device_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the network settings body blank.
void draw_wifi_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the Home Assistant settings body blank.
void draw_home_assistant_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the MQTT discovery settings body blank.
void draw_mqtt_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the ADS-B traffic settings body blank.
void draw_air_traffic_settings_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws only active screen-saver editing UI.
void draw_screen_saver_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the weather-source selection page under Settings.
void draw_weather_sources_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Leaves the time-zone settings body blank.
void draw_time_zone_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the keypad-debug diagnostics page.
void draw_keypad_debug_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Placeholder for the future alignment menu page.
void draw_alignment_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the 2x2 ordered-dither greyscale test card.
void draw_greyscale_test_card(uint8_t* fb, const ConsoleState& console_state);

} // namespace settings_screens
