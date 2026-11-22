#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "console_model.h"

/// @brief One user-configured watchlist entry, stored independently of the
/// live/placeholder quote data share_price_manager attaches at runtime.
struct WatchedShareConfig
{
    std::array<char, 10> symbol;
    std::array<char, 24> display_name;
};

/// @brief Compares two watched-share config entries field-by-field.
/// @details Needed so std::array<WatchedShareConfig, N>::operator== has an
/// element comparison to call -- used by share_price_manager.cpp to detect a
/// watchlist edit at runtime.
inline bool operator==(const WatchedShareConfig& lhs, const WatchedShareConfig& rhs)
{
    return lhs.symbol == rhs.symbol && lhs.display_name == rhs.display_name;
}

inline bool operator!=(const WatchedShareConfig& lhs, const WatchedShareConfig& rhs)
{
    return !(lhs == rhs);
}

/// @brief Storage for a Home Assistant long-lived access token, including the
/// terminator.
/// @details HA tokens are JWTs of about 183 characters. The old 128-byte field
/// rejected every real token, so the web form could never enable HA. 256
/// leaves headroom for longer issuer/claim strings.
inline constexpr size_t kHomeAssistantTokenCapacity = 256U;

/// @brief Web PIN salt and SHA-256 hash sizes (#158).
inline constexpr size_t kWebPinSaltBytes = 16U;
inline constexpr size_t kWebPinHashBytes = 32U;

/// @brief Persistent runtime configuration for one Merlin CCU.
/// @details The structure is deliberately fixed-size so it can be stored
/// directly in flash with simple versioning and CRC checks. Empty strings mean
/// "use the compile-time fallback" for legacy settings.
struct RuntimeConfig
{
    std::array<char, 32> device_name;
    std::array<char, 32> device_label;
    std::array<char, 32> location;
    std::array<char, 32> room;
    bool remote_config_enabled;

    std::array<char, 33> wifi_ssid;
    std::array<char, 64> wifi_password;

    bool home_assistant_enabled;
    std::array<char, 64> home_assistant_host;
    uint16_t home_assistant_port;
    std::array<char, kHomeAssistantTokenCapacity> home_assistant_token;
    std::array<char, 64> home_assistant_entity_id;
    std::array<char, 64> home_assistant_self_entity_id;
    std::array<char, 64> weather_entity_id;
    std::array<char, 64> sun_entity_id;
    std::array<char, 64> weather_coordinates;

    bool mqtt_enabled;
    std::array<char, 64> mqtt_host;
    uint16_t mqtt_port;
    std::array<char, 64> mqtt_username;
    std::array<char, 64> mqtt_password;
    std::array<char, 32> mqtt_discovery_prefix;
    std::array<char, 64> mqtt_base_topic;

    WeatherSource weather_source;
    TimeZoneSelection time_zone;
    ScreenSaverSelection screen_saver;
    uint16_t screen_saver_timeout_minutes;

    uint8_t watched_share_count;
    std::array<WatchedShareConfig, kMaxWatchedShares> watched_shares;

    /// @brief Local share-price feed (issue #42), reusing home_assistant_host/
    /// home_assistant_port -- kept as its own enable flag and token rather
    /// than folded into the Home Assistant section, so the feed can be
    /// switched off without touching HA. See docs/share-feed-design.md.
    bool shares_feed_enabled;
    std::array<char, 128> shares_feed_token;

    /// @brief Optional web config PIN (#158), stored only as SHA-256 of the
    /// salt followed by the PIN. No PIN means the web pages are open.
    bool web_pin_set;
    std::array<uint8_t, kWebPinSaltBytes> web_pin_salt;
    std::array<uint8_t, kWebPinHashBytes> web_pin_hash;
};

namespace config_manager
{

/// @brief Where a settings change came from, for the change record (#158).
enum class ChangeSource : uint8_t
{
    Panel,
    Web,
};

/// @brief The last settings change: when, from where, e.g. "21:45 web
/// 192.168.1.23". Kept in RAM, so a restart clears it.
struct ChangeRecord
{
    bool valid;
    std::array<char, 40> text;
};

/// @brief Total bytes reserved at the tail of flash for the settings store.
/// @details Other flash-backed persistence (e.g. Pinter tracking state in
/// pinter_store.cpp) derives its own storage offset from this so reserved
/// regions can never overlap, even if this store's slot count changes later.
/// config_manager.cpp statically asserts this matches its real,
/// hardware-derived reservation.
inline constexpr uint32_t kReservedFlashBytes = 4096U * 2U;

/// @brief Loads persistent configuration from flash or creates defaults.
void init();

/// @brief Returns the active runtime configuration.
const RuntimeConfig& settings();

/// @brief Updates the active configuration immediately and queues it to be
/// written to flash.
/// @details The new settings are visible via settings() right away; the
/// actual flash write is deferred to flush_pending_save(). save() is
/// reachable synchronously from web request handling (a settings-page button
/// press or the web config form's Save action), and
/// a flash erase/program disables all interrupts for its duration -- doing
/// that while still nested inside a network request has caused visible
/// multi-second UI stalls in practice (see the same issue with Pinter
/// persistence). Always returns true; the underlying flash write is no
/// longer synchronous, so a real write failure can only be caught (and
/// logged) later, inside flush_pending_save().
/// @param source where the change came from, for the change record.
/// @param detail extra text for the record, such as the browser's address.
bool save(const RuntimeConfig& settings, ChangeSource source = ChangeSource::Panel,
          const char* detail = nullptr);

/// @brief The last settings change since start-up (#158).
ChangeRecord last_change();

/// @brief Writes any pending settings save to flash, if one is due.
/// @details Must be called from a point in the main loop that is not nested
/// inside network request handling -- see save()'s docs for why.
bool flush_pending_save();

/// @brief Returns a normalized device name suitable for hostnames and UI labels.
const char* device_name();

} // namespace config_manager
