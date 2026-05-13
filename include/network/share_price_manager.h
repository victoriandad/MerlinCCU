#pragma once

#include "console_model.h"

namespace share_price_manager
{

/// @brief Initialises the no-account share-price fetcher.
/// @details The first implementation watches BAE Systems (`BA.L`) through
/// Yahoo Finance chart JSON because it provides current price metadata and
/// graph history without an API key.
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
