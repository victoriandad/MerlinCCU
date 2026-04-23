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

/// @brief Copies the latest Wi-Fi status into the logical front-panel state.
bool set_wifi_status(const WifiStatus& wifi_status);

/// @brief Copies the latest time status into the logical front-panel state.
bool set_time_status(const TimeStatus& time_status);

/// @brief Copies the latest Home Assistant status into the logical front-panel state.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status);

/// @brief Copies the latest MQTT status into the logical front-panel state.
bool set_mqtt_status(const MqttStatus& mqtt_status);

/// @brief Copies the latest raw keypad monitor snapshot into the UI state.
bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status);

/// @brief Overrides the displayed text for one softkey using controller-owned storage.
/// @details Pass `nullptr` or an empty string to clear the override and restore
/// the page-default label. Rendering will wrap to two lines when needed and
/// truncate any content that would exceed two lines on screen.
bool set_softkey_label(SoftKeyId key, const char* label);

/// @brief Applies one button event to the logical front-panel state.
/// @details For now this is a pure software state machine. It does not require
/// the keypad hardware to exist yet.
bool handle_button_event(const ButtonEvent& event);

} // namespace console_controller
