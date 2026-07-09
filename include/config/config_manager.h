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

namespace config_manager
{

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
/// actual flash write is deferred to flush_pending_save(). Both save() and
/// reset_to_defaults() are reachable synchronously from web request handling
/// (a settings-page button press or the web config form's Save action), and
/// a flash erase/program disables all interrupts for its duration -- doing
/// that while still nested inside a network request has caused visible
/// multi-second UI stalls in practice (see the same issue with Pinter
/// persistence). Always returns true; the underlying flash write is no
/// longer synchronous, so a real write failure can only be caught (and
/// logged) later, inside flush_pending_save().
bool save(const RuntimeConfig& settings);

/// @brief Restores factory defaults and queues them to be written to flash.
bool reset_to_defaults();

/// @brief Writes any pending settings save to flash, if one is due.
/// @details Must be called from a point in the main loop that is not nested
/// inside network request handling -- see save()'s docs for why.
bool flush_pending_save();

/// @brief Returns a normalized device name suitable for hostnames and UI labels.
const char* device_name();

} // namespace config_manager
