#include "config_persistence.h"

#include <algorithm>
#include <cstdio>

#include "text_utils.h"

namespace config_persistence
{

namespace
{

using text_utils::copy_text;

} // namespace

uint32_t crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }

    return ~crc;
}

bool validate_slot(const ConfigSlot& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kConfigVersion ||
        slot.payload_size != sizeof(RuntimeConfig))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(RuntimeConfig));
    return slot.crc32 == expected;
}

bool validate_legacy_slot_v1(const LegacyConfigSlotV1& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kLegacyConfigVersion ||
        slot.payload_size != sizeof(LegacyRuntimeConfigV1))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(LegacyRuntimeConfigV1));
    return slot.crc32 == expected;
}

bool validate_legacy_slot_v2(const LegacyConfigSlotV2& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kLegacyConfigVersionV2 ||
        slot.payload_size != sizeof(LegacyRuntimeConfigV2))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(LegacyRuntimeConfigV2));
    return slot.crc32 == expected;
}

bool validate_legacy_slot_v3(const LegacyConfigSlotV3& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kLegacyConfigVersionV3 ||
        slot.payload_size != sizeof(LegacyRuntimeConfigV3))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(LegacyRuntimeConfigV3));
    return slot.crc32 == expected;
}

std::array<WatchedShareConfig, kMaxWatchedShares> default_watched_shares()
{
    std::array<WatchedShareConfig, kMaxWatchedShares> shares = {};
    copy_text(shares[0].symbol, "BA.L");
    copy_text(shares[0].display_name, "BAE SYSTEMS");
    return shares;
}

RuntimeConfig migrate_legacy_settings_v3(const LegacyRuntimeConfigV3& legacy)
{
    RuntimeConfig migrated = {};
    migrated.device_name = legacy.device_name;
    migrated.device_label = legacy.device_label;
    migrated.location = legacy.location;
    migrated.room = legacy.room;
    migrated.remote_config_enabled = legacy.remote_config_enabled;
    migrated.wifi_ssid = legacy.wifi_ssid;
    migrated.wifi_password = legacy.wifi_password;
    migrated.home_assistant_enabled = legacy.home_assistant_enabled;
    migrated.home_assistant_host = legacy.home_assistant_host;
    migrated.home_assistant_port = legacy.home_assistant_port;
    migrated.home_assistant_token = legacy.home_assistant_token;
    migrated.home_assistant_entity_id = legacy.home_assistant_entity_id;
    migrated.home_assistant_self_entity_id = legacy.home_assistant_self_entity_id;
    migrated.weather_entity_id = legacy.weather_entity_id;
    migrated.sun_entity_id = legacy.sun_entity_id;
    migrated.weather_coordinates = legacy.weather_coordinates;
    migrated.mqtt_enabled = legacy.mqtt_enabled;
    migrated.mqtt_host = legacy.mqtt_host;
    migrated.mqtt_port = legacy.mqtt_port;
    migrated.mqtt_username = legacy.mqtt_username;
    migrated.mqtt_password = legacy.mqtt_password;
    migrated.mqtt_discovery_prefix = legacy.mqtt_discovery_prefix;
    migrated.mqtt_base_topic = legacy.mqtt_base_topic;
    migrated.air_traffic_enabled = legacy.air_traffic_enabled;
    migrated.air_traffic_host = legacy.air_traffic_host;
    migrated.air_traffic_port = legacy.air_traffic_port;
    migrated.air_traffic_api_key = legacy.air_traffic_api_key;
    migrated.air_traffic_coordinates = legacy.air_traffic_coordinates;
    migrated.air_traffic_radius_nm = legacy.air_traffic_radius_nm;
    migrated.weather_source = legacy.weather_source;
    migrated.time_zone = legacy.time_zone;
    migrated.screen_saver = legacy.screen_saver;
    migrated.screen_saver_timeout_minutes = legacy.screen_saver_timeout_minutes;
    migrated.watched_share_count = legacy.watched_share_count;
    migrated.watched_shares = legacy.watched_shares;
    migrated.shares_feed_enabled = false;
    copy_text(migrated.shares_feed_token, "");
    return migrated;
}

RuntimeConfig migrate_legacy_settings_v2(const LegacyRuntimeConfigV2& legacy)
{
    RuntimeConfig migrated = {};
    migrated.device_name = legacy.device_name;
    migrated.device_label = legacy.device_label;
    migrated.location = legacy.location;
    migrated.room = legacy.room;
    migrated.remote_config_enabled = legacy.remote_config_enabled;
    migrated.wifi_ssid = legacy.wifi_ssid;
    migrated.wifi_password = legacy.wifi_password;
    migrated.home_assistant_enabled = legacy.home_assistant_enabled;
    migrated.home_assistant_host = legacy.home_assistant_host;
    migrated.home_assistant_port = legacy.home_assistant_port;
    migrated.home_assistant_token = legacy.home_assistant_token;
    migrated.home_assistant_entity_id = legacy.home_assistant_entity_id;
    migrated.home_assistant_self_entity_id = legacy.home_assistant_self_entity_id;
    migrated.weather_entity_id = legacy.weather_entity_id;
    migrated.sun_entity_id = legacy.sun_entity_id;
    migrated.weather_coordinates = legacy.weather_coordinates;
    migrated.mqtt_enabled = legacy.mqtt_enabled;
    migrated.mqtt_host = legacy.mqtt_host;
    migrated.mqtt_port = legacy.mqtt_port;
    migrated.mqtt_username = legacy.mqtt_username;
    migrated.mqtt_password = legacy.mqtt_password;
    migrated.mqtt_discovery_prefix = legacy.mqtt_discovery_prefix;
    migrated.mqtt_base_topic = legacy.mqtt_base_topic;
    migrated.air_traffic_enabled = legacy.air_traffic_enabled;
    migrated.air_traffic_host = legacy.air_traffic_host;
    migrated.air_traffic_port = legacy.air_traffic_port;
    migrated.air_traffic_api_key = legacy.air_traffic_api_key;
    migrated.air_traffic_coordinates = legacy.air_traffic_coordinates;
    migrated.air_traffic_radius_nm = legacy.air_traffic_radius_nm;
    migrated.weather_source = legacy.weather_source;
    migrated.time_zone = legacy.time_zone;
    migrated.screen_saver = legacy.screen_saver;
    migrated.screen_saver_timeout_minutes = legacy.screen_saver_timeout_minutes;
    migrated.watched_share_count = 1U;
    migrated.watched_shares = default_watched_shares();
    migrated.shares_feed_enabled = false;
    copy_text(migrated.shares_feed_token, "");
    return migrated;
}

RuntimeConfig migrate_legacy_settings(const LegacyRuntimeConfigV1& legacy)
{
    RuntimeConfig migrated = {};
    migrated.device_name = legacy.device_name;
    migrated.device_label = legacy.device_label;
    migrated.location = legacy.location;
    migrated.room = legacy.room;
    migrated.remote_config_enabled = legacy.remote_config_enabled;
    migrated.wifi_ssid = legacy.wifi_ssid;
    migrated.wifi_password = legacy.wifi_password;
    migrated.home_assistant_enabled = legacy.home_assistant_enabled;
    migrated.home_assistant_host = legacy.home_assistant_host;
    migrated.home_assistant_port = legacy.home_assistant_port;
    migrated.home_assistant_token = legacy.home_assistant_token;
    migrated.home_assistant_entity_id = legacy.home_assistant_entity_id;
    migrated.home_assistant_self_entity_id = legacy.home_assistant_self_entity_id;
    migrated.weather_entity_id = legacy.weather_entity_id;
    migrated.sun_entity_id = legacy.sun_entity_id;
    copy_text(migrated.weather_coordinates, "");
    migrated.mqtt_enabled = legacy.mqtt_enabled;
    migrated.mqtt_host = legacy.mqtt_host;
    migrated.mqtt_port = legacy.mqtt_port;
    migrated.mqtt_username = legacy.mqtt_username;
    migrated.mqtt_password = legacy.mqtt_password;
    migrated.mqtt_discovery_prefix = legacy.mqtt_discovery_prefix;
    migrated.mqtt_base_topic = legacy.mqtt_base_topic;
    migrated.weather_source = legacy.weather_source;
    migrated.time_zone = legacy.time_zone;
    migrated.screen_saver = legacy.screen_saver;
    migrated.screen_saver_timeout_minutes = legacy.screen_saver_timeout_minutes;
    migrated.watched_share_count = 1U;
    migrated.watched_shares = default_watched_shares();
    migrated.shares_feed_enabled = false;
    copy_text(migrated.shares_feed_token, "");
    return migrated;
}

RuntimeConfig make_default_settings()
{
    RuntimeConfig settings = {};
    copy_text(settings.device_name, kDefaultDeviceName);
    copy_text(settings.device_label, "Merlin CCU");
    copy_text(settings.location, "");
    copy_text(settings.room, "");
    settings.remote_config_enabled = true;

    copy_text(settings.wifi_ssid, "");
    copy_text(settings.wifi_password, "");

    settings.home_assistant_enabled = false;
    copy_text(settings.home_assistant_host, "");
    settings.home_assistant_port = 8123;
    copy_text(settings.home_assistant_token, "");
    copy_text(settings.home_assistant_entity_id, "");
    copy_text(settings.home_assistant_self_entity_id, "");
    copy_text(settings.weather_entity_id, "");
    copy_text(settings.sun_entity_id, "");
    copy_text(settings.weather_coordinates, "");

    settings.mqtt_enabled = false;
    copy_text(settings.mqtt_host, "");
    settings.mqtt_port = 1883;
    copy_text(settings.mqtt_username, "");
    copy_text(settings.mqtt_password, "");
    copy_text(settings.mqtt_discovery_prefix, "homeassistant");
    copy_text(settings.mqtt_base_topic, "merlinccu");

    settings.air_traffic_enabled = false;
    copy_text(settings.air_traffic_host, "api.adsb.lol");
    settings.air_traffic_port = 80;
    copy_text(settings.air_traffic_api_key, "");
    copy_text(settings.air_traffic_coordinates, "");
    settings.air_traffic_radius_nm = 30;

    settings.weather_source = WeatherSource::HomeAssistant;
    settings.time_zone = TimeZoneSelection::EuropeLondon;
    settings.screen_saver = ScreenSaverSelection::Life;
    settings.screen_saver_timeout_minutes = 5;

    settings.watched_share_count = 1U;
    settings.watched_shares = default_watched_shares();

    settings.shares_feed_enabled = false;
    copy_text(settings.shares_feed_token, "");
    return settings;
}

/// @brief Drops blank-symbol rows and clamps the count to what remains.
/// @details The web config form can post a sparse list (a row cleared out of
/// the middle); compacting here keeps share_price_manager's consumer-side
/// loop simple -- it only ever needs to trust watched_share_count.
void compact_watched_shares(RuntimeConfig& settings)
{
    std::array<WatchedShareConfig, kMaxWatchedShares> compacted = {};
    uint8_t compacted_count = 0U;
    const uint8_t scan_count =
        std::min(settings.watched_share_count, static_cast<uint8_t>(kMaxWatchedShares));
    for (uint8_t i = 0U; i < scan_count; ++i)
    {
        if (settings.watched_shares[i].symbol[0] == '\0')
        {
            continue;
        }
        compacted[compacted_count] = settings.watched_shares[i];
        if (compacted[compacted_count].display_name[0] == '\0')
        {
            copy_text(compacted[compacted_count].display_name,
                      compacted[compacted_count].symbol.data());
        }
        ++compacted_count;
    }

    settings.watched_shares = compacted;
    settings.watched_share_count = compacted_count;
}

bool should_auto_enable_home_assistant(const RuntimeConfig& settings)
{
    return !settings.home_assistant_enabled && settings.home_assistant_host[0] != '\0' &&
           settings.home_assistant_token[0] != '\0' && settings.home_assistant_entity_id[0] != '\0';
}

RuntimeConfig sanitize_settings(const RuntimeConfig& settings)
{
    RuntimeConfig sanitized = settings;
    if (sanitized.device_name[0] == '\0')
    {
        copy_text(sanitized.device_name, kDefaultDeviceName);
    }
    if (sanitized.home_assistant_port == 0)
    {
        sanitized.home_assistant_port = 8123;
    }
    if (sanitized.mqtt_port == 0)
    {
        sanitized.mqtt_port = 1883;
    }
    if (sanitized.air_traffic_port == 0)
    {
        sanitized.air_traffic_port = 80;
    }
    if (sanitized.air_traffic_radius_nm == 0)
    {
        sanitized.air_traffic_radius_nm = 30;
    }
    sanitized.air_traffic_radius_nm = std::min(sanitized.air_traffic_radius_nm, kMaxAirTrafficRadiusNm);
    sanitized.screen_saver_timeout_minutes =
        std::min(sanitized.screen_saver_timeout_minutes, kMaxScreenSaverTimeoutMinutes);
    if (sanitized.weather_source == WeatherSource::MetNorway)
    {
        sanitized.weather_source = WeatherSource::OpenMeteo;
    }
    if (should_auto_enable_home_assistant(sanitized))
    {
        sanitized.home_assistant_enabled = true;
    }
    compact_watched_shares(sanitized);
    return sanitized;
}

const ConfigCandidate* choose_newest_candidate(const ConfigCandidate& a, const ConfigCandidate& b)
{
    if (!a.valid && !b.valid)
    {
        return nullptr;
    }
    if (a.valid && b.valid)
    {
        return (b.sequence > a.sequence) ? &b : &a;
    }
    return a.valid ? &a : &b;
}

} // namespace config_persistence
