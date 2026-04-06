#pragma once

#include "console_model.h"

namespace home_assistant_manager {

/// @brief Initializes the Home Assistant connection state.
void init();

/// @brief Advances the Home Assistant connection state machine.
bool update(const WifiStatus& wifi_status);

/// @brief Returns the latest Home Assistant status snapshot for the UI/controller.
const HomeAssistantStatus& status();

}  // namespace home_assistant_manager
