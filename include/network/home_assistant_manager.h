#pragma once

#include <cstdint>

#include "console_model.h"

namespace home_assistant_manager
{

/// @brief How often the Home Assistant data, including weather from either
/// source, is refreshed.
inline constexpr uint32_t kRefreshIntervalMs = 5U * 60U * 1000U;

/// @brief Age at which weather data counts as stale: four missed refreshes.
/// @details Owned here, by the subsystem that fetches the data, so every page
/// that shows weather freshness uses the same threshold (STATE-4).
inline constexpr uint32_t kWeatherStaleAfterMs = 4U * kRefreshIntervalMs;

/// @brief Returns true unless the latest weather fetch failed or the source
/// is switched off or not set up.
/// @details A refresh passes through Resolving, Connecting and Authorizing
/// before it returns to Connected; the data from the previous fetch is still
/// current during those states, so only a failure or a missing source makes it
/// not current. Its age alone then decides LIVE or STALE.
constexpr bool weather_fetch_ok(HomeAssistantConnectionState state)
{
    switch (state)
    {
    case HomeAssistantConnectionState::Disabled:
    case HomeAssistantConnectionState::Unconfigured:
    case HomeAssistantConnectionState::Unauthorized:
    case HomeAssistantConnectionState::Error:
        return false;
    case HomeAssistantConnectionState::WaitingForWifi:
    case HomeAssistantConnectionState::Resolving:
    case HomeAssistantConnectionState::Connecting:
    case HomeAssistantConnectionState::Authorizing:
    case HomeAssistantConnectionState::Connected:
        return true;
    }
    return false;
}

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
