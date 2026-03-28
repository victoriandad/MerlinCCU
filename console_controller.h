#pragma once

#include "console_model.h"
#include "input.h"

namespace console_controller {

/// @brief Initializes the front-panel state model.
void init();

/// @brief Returns the current logical front-panel state.
const ConsoleState& state();

/// @brief Applies one button event to the logical front-panel state.
/// @details For now this is a pure software state machine. It does not require
/// the keypad hardware to exist yet.
void handle_button_event(const ButtonEvent& event);

}  // namespace console_controller
