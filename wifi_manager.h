#pragma once

#include "console_model.h"

namespace wifi_manager {

/// @brief Initializes the Pico W Wi-Fi stack and starts a station connection if configured.
void init();

/// @brief Advances the Wi-Fi state machine and reports whether visible status changed.
bool update();

/// @brief Returns the latest Wi-Fi status snapshot for the UI/controller.
const WifiStatus& status();

}  // namespace wifi_manager
