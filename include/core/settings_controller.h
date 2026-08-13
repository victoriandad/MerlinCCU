#pragma once

#include <cstdint>

#include "console_model.h"
#include "input.h"

/// @brief Settings route handling and softkey-caption formatting, split out
/// of console_controller.cpp -- issue #44 (staged, following the
/// pinter_controller.h/alert_controller.h/calendar_controller.h precedent).
/// @details Every function here takes the console's ConsoleState explicitly
/// (where it needs one) rather than reaching for a hidden global, matching
/// the pattern already used by the display split (issue #45). A few small
/// state toggles that aren't strictly "Settings" (share-slot selection,
/// air-traffic view mode/paging) are bundled in here too: none of them
/// warranted a module of their own, and they share this module's shape --
/// a small state mutation plus an optional softkey caption.
namespace settings_controller
{

/// @brief Number of top-level Settings menu section-group pages.
inline constexpr uint8_t kSettingsPageCount = 2U;

/// @brief Static metadata for one selectable weather source.
struct WeatherSourceDefinition
{
    WeatherSource source;
    const char* selection_label;
    const char* option_label;
};

/// @brief Static metadata for one selectable time-zone preset.
struct TimeZoneDefinition
{
    TimeZoneSelection zone;
    const char* selection_label;
    const char* option_label;
};

/// @brief Static metadata for one selectable screen saver.
struct ScreenSaverDefinition
{
    ScreenSaverSelection selection;
    const char* selection_label;
    const char* option_label;
};

/// @brief Returns a short on/off label for selection-style softkeys.
const char* enabled_selection_text(bool enabled);

/// @brief Returns whether a saved secret has a non-empty persisted value.
const char* secret_selection_text(bool present);

/// @brief Returns the operator-facing identity label used on the settings menu.
const char* device_identity_selection_text();

/// @brief Formats a numeric port for bracketed softkey labels.
const char* port_selection_text(SoftKeyId key, uint16_t port);

/// @brief Formats a nautical-mile radius for bracketed softkey labels.
const char* radius_nm_selection_text(SoftKeyId key, uint16_t radius_nm);

/// @brief Moves the top-level settings menu between paged section groups.
bool change_page(ConsoleState& console_state, int direction);

/// @brief Decodes the currently observed matrix closure set into one key legend.
const char* decoded_pressed_key(const KeypadMonitorStatus& keypad_status);

/// @brief Returns the static metadata for one selectable weather source.
const WeatherSourceDefinition& weather_source_definition(WeatherSource source);

/// @brief Returns the static metadata for one selectable time-zone preset.
const TimeZoneDefinition& time_zone_definition(TimeZoneSelection zone);

/// @brief Returns the static metadata for one selectable screen saver.
const ScreenSaverDefinition& screen_saver_definition(ScreenSaverSelection selection);

/// @brief Returns the selectable time zone at a relative offset from the current one.
const TimeZoneDefinition* relative_time_zone_definition(const ConsoleState& console_state,
                                                        int offset);

/// @brief Returns the configured or connected Wi-Fi name for the settings menu.
const char* wifi_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently selected weather-source label for menu softkeys.
const char* weather_source_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently selected weather period label for menu softkeys.
const char* weather_period_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently selected share history period label.
const char* share_period_selection_text(const ConsoleState& console_state);

/// @brief Returns the Local Traffic page's current presentation mode label.
const char* air_traffic_view_mode_selection_text(const ConsoleState& console_state);

/// @brief Formats local temperature for softkey value brackets.
const char* local_temperature_selection_text(const ConsoleState& console_state);

/// @brief Formats local humidity for softkey value brackets.
const char* local_humidity_selection_text(const ConsoleState& console_state);

/// @brief Formats local pressure for softkey value brackets.
const char* local_pressure_selection_text(const ConsoleState& console_state);

/// @brief Formats the local VOC-change band for softkey value brackets.
const char* local_air_quality_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently selected screen-saver label for menu softkeys.
const char* screen_saver_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently selected time-zone label for menu softkeys.
const char* time_zone_selection_text(const ConsoleState& console_state);

/// @brief Returns the currently configured screen-saver timeout label.
const char* screen_saver_timeout_selection_text(const ConsoleState& console_state);

/// @brief Updates the selected weather source when a new provider is chosen.
bool select_weather_source(WeatherSource source);

/// @brief Advances the active weather page range without touching persisted config.
bool cycle_weather_period(ConsoleState& console_state);

/// @brief Advances the active share detail period without touching persisted config.
bool cycle_share_period(ConsoleState& console_state);

/// @brief Toggles the Local Traffic page between the Tabular and Plot views.
bool toggle_air_traffic_view_mode(ConsoleState& console_state);

/// @brief Returns the number of tabular Local Traffic pages for the current aircraft count.
uint8_t air_traffic_page_count(const ConsoleState& console_state);

/// @brief Opens the requested share detail page from the current watchlist.
bool select_share_slot(ConsoleState& console_state, uint8_t slot);

/// @brief Opens the currently selected share detail page from the watchlist.
bool open_selected_share_detail(ConsoleState& console_state);

/// @brief Updates the selected time zone by moving relative to the current choice.
bool select_relative_time_zone(ConsoleState& console_state, int offset);

/// @brief Updates the selected screen saver when the user chooses a new stub.
bool select_screen_saver(ScreenSaverSelection selection);

/// @brief Toggles whether the local web configuration server may run.
bool toggle_remote_config_enabled();

/// @brief Toggles the Home Assistant REST integration enable flag.
bool toggle_home_assistant_enabled();

/// @brief Toggles the MQTT discovery integration enable flag.
bool toggle_mqtt_enabled();

/// @brief Toggles the local ADS-B air-traffic feed enable flag.
bool toggle_air_traffic_enabled();

/// @brief Leaves the timeout scratchpad and restores normal page navigation.
bool stop_timeout_editing(ConsoleState& console_state);

/// @brief Enters the timeout scratchpad using the currently saved timeout.
bool start_timeout_editing(ConsoleState& console_state);

/// @brief Handles digit-only timeout entry while the scratchpad is visible.
bool handle_timeout_edit_event(ConsoleState& console_state, const ButtonEvent& event);

/// @brief Persists the scratchpad timeout value when the user presses Enter.
bool confirm_timeout_edit(ConsoleState& console_state);

} // namespace settings_controller
