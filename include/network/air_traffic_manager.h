#pragma once

#include "console_model.h"

/// @brief Local ADS-B air-traffic feed (issue #74).
/// @details Follows the same non-blocking altcp/DNS state-machine shape as
/// `home_assistant_manager.cpp`/`share_price_manager.cpp`. Plain HTTP only
/// (adsb.lol and local dump1090/readsb receivers are typically HTTP, not
/// HTTPS), which is simpler than the TLS path those other managers need.
/// See docs/adsb-air-traffic-feature-design.md for the full design.
namespace air_traffic_manager
{

/// @brief Initialises air-traffic status to its unconfigured default.
void init();

/// @brief Advances the asynchronous air-traffic fetch state machine.
/// @param wifi_status Latest Wi-Fi status snapshot used to determine whether
/// internet requests can start.
/// @param fetch_enabled When `false`, network activity is paused and any
/// in-flight request is cancelled. This keeps the feature isolated to its own
/// page, matching the pattern already used for share-price fetches.
/// @return `true` when visible air-traffic data or status changed.
bool update(const WifiStatus& wifi_status, bool fetch_enabled);

/// @brief Returns the latest display-ready air-traffic snapshot.
const AirTrafficStatus& status();

} // namespace air_traffic_manager
