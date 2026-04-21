#pragma once

#include "console_model.h"

namespace wifi_manager
{

/// @brief Initializes the Pico W Wi-Fi stack and starts a station connection if configured.
/// @details This also captures MAC/hostname information and prepares the
/// periodic internet reachability probe used by the status page.
void init();

/// @brief Advances the Wi-Fi state machine and reports whether visible status changed.
/// @details This is intended to be called from the main loop.
bool update();

/// @brief Returns the latest Wi-Fi status snapshot for the UI/controller.
const WifiStatus& status();

} // namespace wifi_manager
