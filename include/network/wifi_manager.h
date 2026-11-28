#pragma once

#include <cstddef>
#include <optional>

#include "console_model.h"

namespace wifi_manager
{

/// @brief Longest network name the CCU advertises. NetBIOS names hold 15
/// characters, and lwIP's netbiosns_set_name() asserts (a panic and a reboot
/// loop on the Pico) on anything longer, so device names are capped here.
constexpr size_t kMaxHostNameLength = 15U;

/// @brief Initializes the Pico W Wi-Fi stack and starts a station connection if configured.
/// @details This also captures MAC/hostname information and prepares the
/// periodic internet reachability probe used by the status page.
void init();

/// @brief Advances the Wi-Fi state machine and reports whether visible status changed.
/// @details This is intended to be called from the main loop.
bool update();

/// @brief Returns the latest Wi-Fi status snapshot for the UI/controller.
const WifiStatus& status();

/// @brief Signal strength in dBm, sampled every 10 s while connected; empty
/// when not connected or not yet read. Safe to call from lwIP callbacks.
std::optional<int> rssi_dbm();

} // namespace wifi_manager
