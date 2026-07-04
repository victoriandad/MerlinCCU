#pragma once

#include "config_manager.h"
#include "console_model.h"
#include "input.h"

namespace console_controller
{

/// @brief Initializes the front-panel state model.
void init();

/// @brief Returns the current logical front-panel state.
const ConsoleState& state();

/// @brief Applies persisted runtime preferences to the visible console state.
bool apply_runtime_config(const RuntimeConfig& settings);

/// @brief Requests a menu redraw after an out-of-band state change.
/// @details This is used by flows such as the embedded web server where the
/// shared configuration can change outside the button-driven menu loop.
void request_redraw();

/// @brief Returns and clears any pending redraw request.
bool consume_redraw_request();

/// @brief Records user activity that originated outside the hardware input scanner.
/// @details Browser preview key presses use this so the main loop can wake or
/// defer the screensaver through the same activity path as physical keys.
void request_user_activity();

/// @brief Returns and clears any pending out-of-band user activity request.
bool consume_user_activity_request();

/// @brief Copies the latest Wi-Fi status into the logical front-panel state.
bool set_wifi_status(const WifiStatus& wifi_status);

/// @brief Copies the latest time status into the logical front-panel state.
bool set_time_status(const TimeStatus& time_status);

/// @brief Copies the latest Home Assistant status into the logical front-panel state.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status);

/// @brief Copies the latest MQTT status into the logical front-panel state.
bool set_mqtt_status(const MqttStatus& mqtt_status);

/// @brief Copies the latest share market-data snapshot into the UI state.
bool set_share_market_status(const ShareMarketStatus& share_market_status);

/// @brief Copies the latest environment sensor discovery snapshot into the UI state.
bool set_environment_sensor_status(
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status);

/// @brief Copies the latest raw keypad monitor snapshot into the UI state.
bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status);

/// @brief Copies foreground main-loop load telemetry into the UI state.
bool set_main_loop_load_status(const MainLoopLoadStatus& status);

/// @brief Overrides the displayed text for one softkey using controller-owned storage.
/// @details Pass `nullptr` or an empty string to clear the override and restore
/// the page-default label. Rendering will wrap to two lines when needed and
/// truncate any content that would exceed two lines on screen.
bool set_softkey_label(SoftKeyId key, const char* label);

/// @brief Scheduling data for one selectable Pinter brew pack.
struct PinterBrewTiming
{
    const char* name;
    uint8_t recommended_brewing_days;
    uint8_t recommended_conditioning_days;
    uint8_t minimum_brewing_days;
    uint8_t minimum_conditioning_days;
    uint8_t optional_cold_crashing_days;
};

/// @brief Returns timing metadata for a Pinter brew-pack catalogue index.
const PinterBrewTiming& pinter_brew_timing(uint8_t brew_index);

/// @brief Applies one button event to the logical front-panel state.
/// @details For now this is a pure software state machine. It does not require
/// the keypad hardware to exist yet.
bool handle_button_event(const ButtonEvent& event);

/// @brief Cycles test lamp state for web-preview testing when TEST key wiring is absent.
bool cycle_test_lamp_preview();

/// @brief Opens the live alert list page.
bool open_alert_page();

} // namespace console_controller
