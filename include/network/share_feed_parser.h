#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure parsing of the local Home Assistant shares feed response
/// (issue #42's `{"shares":[...]}` contract), extracted from
/// `share_price_manager.cpp` (issue #72). No lwIP/Pico SDK dependency, so
/// this is host-testable (see tests/host/test_share_feed_parser.cpp).
namespace share_feed
{

/// @brief Parses a `{"shares":[...]}` response spanning `[body, buffer_end)`
/// and updates every already-watched share (matched by `"symbol"`) found in
/// it. Shares the feed doesn't mention keep whatever they last showed rather
/// than being blanked. A per-share `"data_state"` other than `"live"` is
/// treated the same as a missing entry. `"price"` accepts either a raw JSON
/// number or a quoted numeric string. Returns true if at least one watched
/// share was updated.
bool parse_shares_feed_response(const char* body, const char* buffer_end,
                                std::array<ShareWatchEntry, kMaxWatchedShares>& watched_shares,
                                uint8_t share_count);

} // namespace share_feed
