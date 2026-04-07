#pragma once

#include "console_model.h"
#include "input.h"

namespace console_controller {

/// @brief Initializes the front-panel state model.
void init();

/// @brief Returns the current logical front-panel state.
const ConsoleState& state();

/// @brief Copies the latest Wi-Fi status into the logical front-panel state.
bool set_wifi_status(const WifiStatus& wifi_status);

/// @brief Copies the latest time status into the logical front-panel state.
bool set_time_status(const TimeStatus& time_status);

/// @brief Copies the latest Home Assistant status into the logical front-panel state.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status);

/// @brief Applies one button event to the logical front-panel state.
/// @details For now this is a pure software state machine. It does not require
/// the keypad hardware to exist yet.
bool handle_button_event(const ButtonEvent& event);

}  // namespace console_controller
