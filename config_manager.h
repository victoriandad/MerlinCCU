#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

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

namespace config_manager
{

/// @brief Loads persistent configuration from flash or creates defaults.
void init();

/// @brief Returns the active runtime configuration.
const RuntimeConfig& settings();

/// @brief Updates the active configuration and persists it to flash.
bool save(const RuntimeConfig& settings);

/// @brief Restores factory defaults and persists them to flash.
bool reset_to_defaults();

/// @brief Returns whether a password satisfies the current web-admin policy.
bool admin_password_matches(const char* password);

/// @brief Returns a normalized device name suitable for hostnames and UI labels.
const char* device_name();

} // namespace config_manager
