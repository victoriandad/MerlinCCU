#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "console_model.h"

/// @brief Weather that Home Assistant publishes over MQTT for every CCU
/// (#153).
/// @details HA's MerlinCCU integration publishes three retained topics under
/// `<base>/house/weather/`: `now`, `hourly` and `daily`. They use the field
/// names of HA's own weather entity, trimmed to what the CCU shows, so the
/// existing HA weather parsers read them. Every payload carries a schema
/// version `v` and the Unix time `ts` it was built. Schema:
/// docs/mqtt-household-data.md. Pure logic, so host tests cover it.
namespace house_weather
{

/// @brief Schema version this firmware reads; other versions are refused.
inline constexpr uint32_t kSchemaVersion = 1U;

/// @brief One of the weather topics.
enum class Topic : uint8_t
{
    Now = 0,
    Hourly,
    Daily,
    /// Official Met Office and flood warnings (#202), read by apply_warnings().
    Warnings,
    /// Weather alerts accepted for every unit (#210), read by
    /// weather_acks::apply().
    Acks,
};

inline constexpr size_t kTopicCount = 5U;

/// @brief Largest payload of each topic the CCU accepts, in bytes.
/// @details HA's integration trims its payloads to fit (MAX_PAYLOAD_BYTES in
/// ha/custom_components/merlinccu/house_weather.py); a bigger one is dropped.
/// The hourly limit leaves room for precipitation and gusts on all 24 rows
/// (#202).
inline constexpr size_t kMaxPayloadBytes[kTopicCount] = {512U, 4096U, 1536U, 512U, 256U};

/// @brief Returns which weather topic `topic` is, for base topic `base`.
std::optional<Topic> topic_for(std::string_view topic, std::string_view base);

/// @brief Applies one payload to `status`.
/// @details `now` replaces the current conditions, provider name and sun
/// times; `hourly` and `daily` replace their forecast tables. Units come from
/// the payload itself, so the topics can arrive in any order.
/// @return The payload's `ts`, or nothing when the payload is refused: a
/// different schema version, no timestamp, or missing its main field
/// (`state` or `forecast`). A refused payload leaves the data shown in place.
std::optional<uint32_t> apply(Topic topic, const char* json, HomeAssistantStatus& status);

/// @brief Applies a `warnings` payload (#202) to `warnings`.
/// @details The payload names the highest `level` (`none`, `yellow`, `amber`
/// or `red`), its `summary` and the `lines` of every warning in force, which
/// become the detail text one per line. Lines that do not fit are left out.
/// `stale` is cleared; the caller ages the warnings from the returned time.
/// @return The payload's `ts`, or nothing when the payload is refused (a
/// different schema version, no timestamp, or an unknown level), leaving
/// `warnings` as it was.
std::optional<uint32_t> apply_warnings(const char* json, WeatherWarnings& warnings);

} // namespace house_weather
