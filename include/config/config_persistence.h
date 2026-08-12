#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config_manager.h"

/// @brief Pure flash-config persistence logic shared between the firmware and
/// host tests (see issue #14/#66 pattern): CRC, slot validation, legacy
/// migration, and settings sanitization. Knows nothing about actual flash
/// reads/writes or hardware addresses -- config_manager.cpp owns those and
/// calls into this for the decision logic. A bug here is user config data
/// loss on next boot, not a cosmetic glitch, which is why it's worth testing
/// directly against synthetic slot data rather than only via manual bring-up.
namespace config_persistence
{

inline constexpr uint32_t kConfigMagic = 0x4D434355U; // "MCCU"
inline constexpr uint16_t kConfigVersion = 3;
inline constexpr uint16_t kLegacyConfigVersion = 1;
inline constexpr uint16_t kLegacyConfigVersionV2 = 2;
inline constexpr uint16_t kMaxScreenSaverTimeoutMinutes = 120U;
/// @brief Widest ADS-B search radius accepted, matching common provider limits.
inline constexpr uint16_t kMaxAirTrafficRadiusNm = 250U;
inline constexpr char kDefaultDeviceName[] = "MerlinCCU";

/// @brief One flash-backed configuration slot (current version).
/// @details Two slots are alternated by sequence number so a power loss during
/// one write still leaves the previous complete configuration available.
struct ConfigSlot
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    RuntimeConfig settings;
};

/// @brief Runtime configuration layout used by config version 1.
/// @details This mirrors the previous flash payload exactly so existing saved
/// settings can be migrated when new fields are added.
struct LegacyRuntimeConfigV1
{
    std::array<char, 32> device_name;
    std::array<char, 32> device_label;
    std::array<char, 32> location;
    std::array<char, 32> room;
    std::array<char, 32> admin_password;
    bool remote_config_enabled;
    bool require_admin_password;

    std::array<char, 33> wifi_ssid;
    std::array<char, 64> wifi_password;

    bool home_assistant_enabled;
    std::array<char, 64> home_assistant_host;
    uint16_t home_assistant_port;
    std::array<char, 128> home_assistant_token;
    std::array<char, 64> home_assistant_entity_id;
    std::array<char, 64> home_assistant_self_entity_id;
    std::array<char, 64> weather_entity_id;
    std::array<char, 64> sun_entity_id;

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
};

struct LegacyConfigSlotV1
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    LegacyRuntimeConfigV1 settings;
};

/// @brief Runtime configuration layout used by config version 2.
/// @details This mirrors the RuntimeConfig payload as it existed immediately
/// before watched-share fields were added, so existing saved settings can be
/// migrated forward without losing Wi-Fi/integration credentials.
struct LegacyRuntimeConfigV2
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
    std::array<char, 128> home_assistant_token;
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

    bool air_traffic_enabled;
    std::array<char, 64> air_traffic_host;
    uint16_t air_traffic_port;
    std::array<char, 128> air_traffic_api_key;
    std::array<char, 64> air_traffic_coordinates;
    uint16_t air_traffic_radius_nm;

    WeatherSource weather_source;
    TimeZoneSelection time_zone;
    ScreenSaverSelection screen_saver;
    uint16_t screen_saver_timeout_minutes;
};

struct LegacyConfigSlotV2
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    LegacyRuntimeConfigV2 settings;
};

/// @brief One configuration candidate read from a flash slot, valid or not.
struct ConfigCandidate
{
    bool valid;
    uint32_t sequence;
    RuntimeConfig settings;
};

/// @brief Calculates a standard CRC-32 (IEEE 802.3 / zlib polynomial) over a byte buffer.
uint32_t crc32(const uint8_t* data, size_t length);

/// @brief Returns true when a stored slot header and payload are valid.
bool validate_slot(const ConfigSlot& slot);

/// @brief Returns true when a stored legacy (v1) slot header and payload are valid.
bool validate_legacy_slot_v1(const LegacyConfigSlotV1& slot);

/// @brief Returns true when a stored legacy (v2) slot header and payload are valid.
bool validate_legacy_slot_v2(const LegacyConfigSlotV2& slot);

/// @brief Converts a validated legacy (v1) settings payload to the current layout.
RuntimeConfig migrate_legacy_settings(const LegacyRuntimeConfigV1& legacy);

/// @brief Converts a validated legacy (v2) settings payload to the current layout.
RuntimeConfig migrate_legacy_settings_v2(const LegacyRuntimeConfigV2& legacy);

/// @brief Returns the default watchlist seeded for new/migrated configurations.
/// @details Reproduces the single BAE Systems placeholder watched pre-#9, so
/// upgrading firmware or a factory reset does not silently empty the shares
/// page.
std::array<WatchedShareConfig, kMaxWatchedShares> default_watched_shares();

/// @brief Builds the factory/default configuration used when flash is empty.
RuntimeConfig make_default_settings();

/// @brief Returns whether HA should be auto-enabled from configured credentials.
/// @details A previous web-form truncation bug could clear only the HA enable
/// checkbox while leaving host, token, and entity fields intact in flash. This
/// guard repairs that partial-save state so integration resumes automatically.
bool should_auto_enable_home_assistant(const RuntimeConfig& settings);

/// @brief Applies the same defaulting/clamping/migration rules every save goes through.
RuntimeConfig sanitize_settings(const RuntimeConfig& settings);

/// @brief Chooses the newer of two read candidates, or nullptr if neither is valid.
/// @details The returned pointer aliases whichever input candidate won; the
/// caller must keep both candidates alive for as long as the result is used.
const ConfigCandidate* choose_newest_candidate(const ConfigCandidate& a, const ConfigCandidate& b);

} // namespace config_persistence
