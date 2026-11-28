#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

/// @brief The unit's state as one small JSON object, for `GET
/// /api/status.json`: the house dashboard's `customapi` widget reads it
/// (docs/network-deployment.md). Pure formatting, so host tests cover it.
namespace status_json
{

/// @brief Largest object build() writes; the dashboard asks for under 1 KB.
inline constexpr size_t kMaxBytes = 640U;

/// @brief What the object reports. Text may be null, which gives "".
struct Status
{
    const char* unit;     ///< Network name, e.g. `merlinccu2`.
    const char* label;    ///< Display label, e.g. `Merlin CCU (Dev)`.
    const char* firmware; ///< Git commit.
    const char* board;    ///< PICO_BOARD.
    uint32_t uptime_s;
    bool wifi_connected;
    std::optional<int> rssi_dbm; ///< null while unknown.
    bool ha_connected;
    bool mqtt_connected;
    uint32_t free_heap;       ///< Heap bytes not in use now.
    const char* current_page; ///< The page's title, or `SCREEN SAVER`.
    uint8_t alerts_active;
    const char* top_alert; ///< Most serious active alert's summary, or "".
    uint8_t units_online;  ///< Other units online (#210).
    uint8_t units_known;
};

/// @brief Writes `status` as JSON into `out`, text escaped.
/// @return False when `out` is too small; `out` then holds an empty object.
bool build(const Status& status, char* out, size_t out_size);

} // namespace status_json
