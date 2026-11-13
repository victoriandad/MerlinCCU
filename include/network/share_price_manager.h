#pragma once

#include "console_model.h"

namespace share_price_manager
{

/// @brief How often the share feed is fetched while the Shares pages are open.
inline constexpr uint32_t kRefreshIntervalMs = 5U * 60U * 1000U;

/// @brief Age at which share data counts as stale: four missed refreshes.
/// @details Owned here, by the subsystem that fetches the data, so every page
/// that shows share freshness uses the same threshold (STATE-4).
inline constexpr uint32_t kShareStaleAfterMs = 4U * kRefreshIntervalMs;

/// @brief Initialises share-price status and any enabled market-data fetcher.
/// @details Direct external provider fetching (the old Yahoo chart path,
/// which caused share-page lockups -- see issue #19) has been replaced by a
/// local Home Assistant/proxy feed (issue #42, see
/// docs/share-feed-design.md). Demo data is shown until
/// RuntimeConfig::shares_feed_enabled is turned on and a fetch succeeds.
void init();

/// @brief Advances the asynchronous market-data state machine.
/// @param wifi_status Latest Wi-Fi status snapshot used to determine whether
/// internet requests can start.
/// @param active_period Share-history period currently selected on the detail page.
/// @param fetch_enabled When `false`, network activity is paused and any
/// in-flight share request is cancelled. This keeps non-share pages isolated
/// from external market-provider failures.
/// @return `true` when visible share data or provider status changed.
bool update(const WifiStatus& wifi_status, SharePeriod active_period, bool fetch_enabled);

/// @brief Returns the latest display-ready share data snapshot.
const ShareMarketStatus& status();

} // namespace share_price_manager
