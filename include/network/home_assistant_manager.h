#pragma once

#include "console_model.h"

namespace home_assistant_manager
{

/// @brief Initializes the Home Assistant REST integration state.
/// @details This parses the local REST endpoint configuration and seeds the
/// one-shot request sequence used by the firmware status page.
void init();

/// @brief Advances the Home Assistant connection state machine.
/// @param wifi_status Latest Wi-Fi status snapshot used to determine whether
/// REST probing can start.
/// @return `true` when visible Home Assistant status changed.
bool update(const WifiStatus& wifi_status);

/// @brief Returns the latest Home Assistant status snapshot for the UI/controller.
const HomeAssistantStatus& status();

} // namespace home_assistant_manager
