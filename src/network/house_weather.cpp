#include "house_weather.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "home_assistant_weather_parser.h"
#include "weather_forecast_parser.h"
#include "weather_json_scan.h"
#include "weather_normalisation.h"

namespace house_weather
{

namespace
{

constexpr std::string_view kTopicNames[kTopicCount] = {"now", "hourly", "daily", "warnings"};

/// @brief The `level` names of a warnings payload, indexed by WarningLevel.
constexpr std::array<std::string_view, 4> kLevelNames = {"none", "yellow", "amber", "red"};

/// @brief Most warning lines read from one payload; HA sends at most this many.
constexpr size_t kMaxWarningLines = 6U;
constexpr std::string_view kWeatherPath = "/house/weather/";

/// @brief Reads a non-negative integer field, e.g. `"ts":1763900000`.
std::optional<uint32_t> read_unsigned(const char* json, const char* key)
{
    char text[16] = {};
    if (!weather_json::extract_json_scalar_value(json, key, text, sizeof(text)) ||
        text[0] < '0' || text[0] > '9')
    {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || value > UINT32_MAX)
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

/// @brief Reads the provider units a forecast payload carries.
void read_units(const char* json, char& temperature_unit, char* wind_unit, size_t wind_unit_size)
{
    char raw[16] = {};
    temperature_unit = '\0';
    if (weather_json::extract_json_string_value(json, "\"temperature_unit\":\"", raw,
                                                sizeof(raw)))
    {
        temperature_unit = weather_normalisation::normalized_temperature_unit(raw);
    }
    wind_unit[0] = '\0';
    if (!weather_json::extract_json_string_value(json, "\"wind_speed_unit\":\"", raw,
                                                 sizeof(raw)) ||
        !weather_normalisation::normalize_wind_speed_unit(raw, wind_unit, wind_unit_size))
    {
        wind_unit[0] = '\0';
    }
}

} // namespace

std::optional<Topic> topic_for(std::string_view topic, std::string_view base)
{
    if (base.empty() || topic.size() <= base.size() + kWeatherPath.size() ||
        topic.substr(0, base.size()) != base ||
        topic.substr(base.size(), kWeatherPath.size()) != kWeatherPath)
    {
        return std::nullopt;
    }
    const std::string_view name = topic.substr(base.size() + kWeatherPath.size());
    for (size_t i = 0; i < kTopicCount; ++i)
    {
        if (name == kTopicNames[i])
        {
            return static_cast<Topic>(i);
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> apply(Topic topic, const char* json, HomeAssistantStatus& status)
{
    if (json == nullptr || read_unsigned(json, "\"v\":") != kSchemaVersion)
    {
        return std::nullopt;
    }
    const std::optional<uint32_t> timestamp = read_unsigned(json, "\"ts\":");
    if (!timestamp.has_value())
    {
        return std::nullopt;
    }

    char temperature_unit = '\0';
    char wind_unit[8] = {};
    switch (topic)
    {
    case Topic::Now:
        if (std::strstr(json, "\"state\":\"") == nullptr)
        {
            return std::nullopt;
        }
        (void)home_assistant_weather::parse_current_weather_entity(
            json, status, temperature_unit, wind_unit, sizeof(wind_unit));
        // Sun times ride along with "now", so the CCU needs no sun entity.
        home_assistant_weather::update_sun_times_from_json(json, status);
        return timestamp;
    case Topic::Hourly:
    case Topic::Daily:
        if (std::strstr(json, "\"forecast\":[") == nullptr)
        {
            return std::nullopt;
        }
        read_units(json, temperature_unit, wind_unit, sizeof(wind_unit));
        if (topic == Topic::Hourly)
        {
            (void)weather_forecast::parse_hourly_forecast_response(json, status, temperature_unit,
                                                                   wind_unit);
        }
        else
        {
            (void)weather_forecast::parse_daily_forecast_response(json, status, temperature_unit,
                                                                  wind_unit);
        }
        return timestamp;
    case Topic::Warnings:
        // Warnings are not weather data; apply_warnings() reads them.
        break;
    }
    return std::nullopt;
}

std::optional<uint32_t> apply_warnings(const char* json, WeatherWarnings& warnings)
{
    if (json == nullptr || read_unsigned(json, "\"v\":") != kSchemaVersion)
    {
        return std::nullopt;
    }
    const std::optional<uint32_t> timestamp = read_unsigned(json, "\"ts\":");
    char level_text[8] = {};
    if (!timestamp.has_value() || !weather_json::extract_json_string_value(
                                      json, "\"level\":\"", level_text, sizeof(level_text)))
    {
        return std::nullopt;
    }
    const auto level = std::find(kLevelNames.begin(), kLevelNames.end(), level_text);
    if (level == kLevelNames.end())
    {
        return std::nullopt;
    }

    WeatherWarnings updated = {};
    updated.level = static_cast<WarningLevel>(std::distance(kLevelNames.begin(), level));
    if (updated.level != WarningLevel::None)
    {
        (void)weather_json::extract_json_string_value(
            json, "\"summary\":\"", updated.summary.data(), updated.summary.size());
        const char* lines = weather_json::find_json_array_start(json, "\"lines\":");
        size_t used = 0U;
        char line[sizeof(updated.detail)] = {};
        for (size_t i = 0U; i < kMaxWarningLines; ++i)
        {
            if (!weather_json::json_array_string_at(lines, i, line, sizeof(line)))
            {
                break;
            }
            const size_t kLength = std::strlen(line);
            const size_t kSeparator = used == 0U ? 0U : 1U;
            if (used + kSeparator + kLength >= updated.detail.size())
            {
                break;
            }
            if (kSeparator != 0U)
            {
                updated.detail[used++] = '\n';
            }
            std::memcpy(updated.detail.data() + used, line, kLength);
            used += kLength;
        }
        updated.detail[used] = '\0';
    }
    warnings = updated;
    return timestamp;
}

} // namespace house_weather
