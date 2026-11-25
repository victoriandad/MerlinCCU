#include "weather_forecast_parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "text_utils.h"
#include "weather_extremes.h"
#include "weather_json_scan.h"
#include "weather_normalisation.h"

namespace weather_forecast
{

namespace
{
using text_utils::copy_text;

constexpr float kMillimetresPerInch = 25.4F;

/// @brief Millimetres in one unit of the payload's `precipitation_unit`.
/// @details HA reports precipitation in mm or inches; mm is assumed when the
/// unit is missing, as it is for the UK providers.
float precipitation_mm_per_unit(const char* json)
{
    char unit[8] = {};
    if (weather_json::extract_json_string_value(json, "\"precipitation_unit\":\"", unit,
                                                sizeof(unit)) &&
        std::strcmp(unit, "in") == 0)
    {
        return kMillimetresPerInch;
    }
    return 1.0F;
}

/// @brief Adds an HA forecast object's precipitation and gusts to `reading`.
/// @details HA has no snowfall field, so a `snowy` period's precipitation is
/// read as snow; any other period's as rain. `pouring` is HA's heavy rain.
/// @return The gust in mph, when the object has one.
std::optional<float> add_precipitation_and_gust(const char* object_json, const char* condition,
                                                const char* wind_source_unit, float mm_per_unit,
                                                weather_extremes::Reading& reading)
{
    if (const std::optional<float> precipitation = read_number(object_json, "\"precipitation\":");
        precipitation.has_value())
    {
        const float kMillimetres = *precipitation * mm_per_unit;
        if (std::strcmp(condition, "snowy") == 0)
        {
            reading.snow_cm = kMillimetres * weather_extremes::kSnowCmPerPrecipitationMm;
        }
        else
        {
            reading.rain_mm = kMillimetres;
        }
    }
    reading.heavy_rain_condition = std::strcmp(condition, "pouring") == 0;

    char gust_text[16] = {};
    float gust_mph = 0.0F;
    if (weather_json::extract_json_scalar_value(object_json, "\"wind_gust_speed\":", gust_text,
                                                sizeof(gust_text)) &&
        weather_normalisation::convert_wind_speed_to_mph_value(gust_text, wind_source_unit,
                                                               &gust_mph))
    {
        reading.gust_mph = gust_mph;
        return gust_mph;
    }
    return std::nullopt;
}

} // namespace

std::optional<float> read_number(const char* json, const char* key)
{
    char text[16] = {};
    float value = 0.0F;
    if (!weather_json::extract_json_scalar_value(json, key, text, sizeof(text)) ||
        !weather_normalisation::parse_float_value(text, &value))
    {
        return std::nullopt;
    }
    return value;
}

void clear_current_weather_metrics(HomeAssistantStatus& status)
{
    status.weather_metrics.current_temperature_celsius_valid = false;
    status.weather_metrics.current_temperature_celsius = 0.0F;
    status.weather_metrics.current_wind_speed_mph_valid = false;
    status.weather_metrics.current_wind_speed_mph = 0.0F;
}

void clear_forecast_weather_metrics(HomeAssistantStatus& status)
{
    status.weather_metrics.forecast_min_temperature_celsius_valid = false;
    status.weather_metrics.forecast_min_temperature_celsius = 0.0F;
    status.weather_metrics.forecast_max_temperature_celsius_valid = false;
    status.weather_metrics.forecast_max_temperature_celsius = 0.0F;
    status.weather_metrics.forecast_max_wind_speed_mph_valid = false;
    status.weather_metrics.forecast_max_wind_speed_mph = 0.0F;
    status.weather_metrics.forecast_max_wind_gust_mph_valid = false;
    status.weather_metrics.forecast_max_wind_gust_mph = 0.0F;
}

void clear_weather_alert_status(HomeAssistantStatus& status)
{
    status.weather_alert_status.provider_warning_active = false;
    status.weather_alert_status.provider_warning_severity = AlertSeverity::None;
    status.weather_alert_status.provider_warning_summary.fill('\0');
    status.weather_alert_status.provider_warning_detail.fill('\0');
}

void include_forecast_temperature_celsius(HomeAssistantStatus& status,
                                          float temperature_celsius)
{
    WeatherMetrics& metrics = status.weather_metrics;
    if (!metrics.forecast_min_temperature_celsius_valid ||
        temperature_celsius < metrics.forecast_min_temperature_celsius)
    {
        metrics.forecast_min_temperature_celsius = temperature_celsius;
        metrics.forecast_min_temperature_celsius_valid = true;
    }

    if (!metrics.forecast_max_temperature_celsius_valid ||
        temperature_celsius > metrics.forecast_max_temperature_celsius)
    {
        metrics.forecast_max_temperature_celsius = temperature_celsius;
        metrics.forecast_max_temperature_celsius_valid = true;
    }
}

void include_forecast_wind_speed_mph(HomeAssistantStatus& status, float wind_speed_mph)
{
    WeatherMetrics& metrics = status.weather_metrics;
    if (!metrics.forecast_max_wind_speed_mph_valid ||
        wind_speed_mph > metrics.forecast_max_wind_speed_mph)
    {
        metrics.forecast_max_wind_speed_mph = wind_speed_mph;
        metrics.forecast_max_wind_speed_mph_valid = true;
    }
}

void include_forecast_wind_gust_mph(HomeAssistantStatus& status, float wind_gust_mph)
{
    WeatherMetrics& metrics = status.weather_metrics;
    if (!metrics.forecast_max_wind_gust_mph_valid ||
        wind_gust_mph > metrics.forecast_max_wind_gust_mph)
    {
        metrics.forecast_max_wind_gust_mph = wind_gust_mph;
        metrics.forecast_max_wind_gust_mph_valid = true;
    }
}

void record_weather_condition_warning(HomeAssistantStatus& status, const char* condition_text,
                                      const char* source_context)
{
    if (!weather_normalisation::weather_condition_is_warning(condition_text))
    {
        return;
    }

    WeatherAlertStatus& warning = status.weather_alert_status;
    warning.provider_warning_active = true;
    warning.provider_warning_severity = AlertSeverity::Warning;
    std::snprintf(warning.provider_warning_summary.data(),
                  warning.provider_warning_summary.size(), "%s", "WX WARNING");
    std::snprintf(warning.provider_warning_detail.data(), warning.provider_warning_detail.size(),
                  "Weather source reports %s%s.\nCheck the latest local forecast before travel.",
                  condition_text, source_context != nullptr ? source_context : "");
}

void clear_weather_forecast(HomeAssistantStatus& status)
{
    status.weather_forecast_count = 0;
    clear_forecast_weather_metrics(status);
    for (auto& entry : status.weather_forecast)
    {
        entry.time_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
        entry.hazards = weather_extremes::kNone;
    }
}

void clear_weather_daily_forecast(HomeAssistantStatus& status)
{
    status.weather_daily_forecast_count = 0;
    for (auto& entry : status.weather_daily_forecast)
    {
        entry.date_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
        entry.hazards = weather_extremes::kNone;
    }
}

bool parse_hourly_forecast_response(const char* json, HomeAssistantStatus& status,
                                    char temperature_unit, const char* wind_source_unit)
{
    if (json == nullptr)
    {
        return false;
    }

    const char* forecast_key = std::strstr(json, "\"forecast\":[");
    if (forecast_key == nullptr)
    {
        return false;
    }

    const char* cursor = std::strchr(forecast_key, '[');
    if (cursor == nullptr)
    {
        return false;
    }
    ++cursor;

    // Start from a blank forecast so a partial parse never leaves stale rows
    // from an earlier successful response on screen.
    clear_weather_forecast(status);
    const float kMmPerUnit = precipitation_mm_per_unit(json);

    while (*cursor != '\0' && *cursor != ']' &&
           status.weather_forecast_count < kWeatherForecastEntryCount)
    {
        const char* object_start = std::strchr(cursor, '{');
        if (object_start == nullptr)
        {
            break;
        }

        const char* object_end = weather_json::find_matching_json_object_end(object_start);
        if (object_end == nullptr)
        {
            break;
        }

        char object_json[384] = {};
        const size_t object_len = static_cast<size_t>(object_end - object_start + 1);
        if (object_len >= sizeof(object_json))
        {
            // Reason for skipping:
            // Copying only part of the object would create invalid JSON and the
            // parser below could then read a half-object as if it were real
            // data. Dropping the single oversized entry is safer than trying to
            // salvage a truncated copy.
            cursor = object_end + 1;
            continue;
        }
        std::memcpy(object_json, object_start, object_len);
        object_json[object_len] = '\0';

        // Each forecast object is re-parsed into the compact, display-friendly
        // fields used by the weather page rather than mirroring the raw JSON.
        char datetime_text[32] = {};
        char temperature_text[16] = {};
        char wind_speed_text[16] = {};
        char wind_bearing_text[16] = {};
        char condition_text[24] = {};

        if (!weather_json::extract_json_string_value(object_json, "\"datetime\":\"",
                                                      datetime_text, sizeof(datetime_text)) ||
            !weather_normalisation::format_hour_text(
                datetime_text, status.weather_forecast[status.weather_forecast_count]
                                   .time_text.data(),
                status.weather_forecast[status.weather_forecast_count].time_text.size()))
        {
            cursor = object_end + 1;
            continue;
        }

        weather_extremes::Reading reading = {};
        if (weather_json::extract_json_scalar_value(object_json, "\"temperature\":",
                                                     temperature_text, sizeof(temperature_text)))
        {
            float temperature_celsius = 0.0F;
            if (weather_normalisation::convert_temperature_to_celsius(
                    temperature_text, temperature_unit, &temperature_celsius))
            {
                include_forecast_temperature_celsius(status, temperature_celsius);
                reading.low_celsius = temperature_celsius;
                reading.high_celsius = temperature_celsius;
            }

            if (temperature_unit != '\0')
            {
                std::snprintf(status.weather_forecast[status.weather_forecast_count]
                                  .temperature_text.data(),
                              status.weather_forecast[status.weather_forecast_count]
                                  .temperature_text.size(),
                              "%s %c", temperature_text, temperature_unit);
            }
            else
            {
                copy_text(
                    status.weather_forecast[status.weather_forecast_count].temperature_text,
                    temperature_text);
            }
        }
        else
        {
            copy_text(status.weather_forecast[status.weather_forecast_count].temperature_text,
                      "-");
        }

        const bool have_wind_speed = weather_json::extract_json_scalar_value(
            object_json, "\"wind_speed\":", wind_speed_text, sizeof(wind_speed_text));
        const bool have_wind_bearing =
            weather_json::extract_json_string_value(object_json, "\"wind_bearing\":\"",
                                                     wind_bearing_text,
                                                     sizeof(wind_bearing_text)) ||
            weather_json::extract_json_scalar_value(object_json, "\"wind_bearing\":",
                                                     wind_bearing_text, sizeof(wind_bearing_text));
        if (have_wind_speed)
        {
            float wind_speed_mph = 0.0F;
            if (weather_normalisation::convert_wind_speed_to_mph_value(
                    wind_speed_text, wind_source_unit, &wind_speed_mph))
            {
                include_forecast_wind_speed_mph(status, wind_speed_mph);
                reading.wind_mph = wind_speed_mph;
            }
        }

        if (!weather_normalisation::format_compact_wind_text(
                have_wind_speed ? wind_speed_text : nullptr,
                have_wind_bearing ? wind_bearing_text : nullptr, wind_source_unit,
                status.weather_forecast[status.weather_forecast_count].wind_text.data(),
                status.weather_forecast[status.weather_forecast_count].wind_text.size()))
        {
            copy_text(status.weather_forecast[status.weather_forecast_count].wind_text, "-");
        }

        if (weather_json::extract_json_string_value(object_json, "\"condition\":\"",
                                                     condition_text, sizeof(condition_text)))
        {
            copy_text(status.weather_forecast[status.weather_forecast_count].condition_text,
                      weather_normalisation::friendly_weather_condition(condition_text));
            record_weather_condition_warning(status, condition_text, " in the hourly forecast");
        }
        else
        {
            copy_text(status.weather_forecast[status.weather_forecast_count].condition_text, "?");
        }

        if (const std::optional<float> gust_mph = add_precipitation_and_gust(
                object_json, condition_text, wind_source_unit, kMmPerUnit, reading);
            gust_mph.has_value())
        {
            include_forecast_wind_gust_mph(status, *gust_mph);
        }
        status.weather_forecast[status.weather_forecast_count].hazards =
            weather_extremes::classify(reading, weather_extremes::Period::Hour);

        ++status.weather_forecast_count;
        cursor = object_end + 1;
    }

    return status.weather_forecast_count > 0;
}

bool parse_daily_forecast_response(const char* json, HomeAssistantStatus& status,
                                   char temperature_unit, const char* wind_source_unit)
{
    if (json == nullptr)
    {
        return false;
    }

    const char* forecast_key = std::strstr(json, "\"forecast\":[");
    if (forecast_key == nullptr)
    {
        return false;
    }

    const char* cursor = std::strchr(forecast_key, '[');
    if (cursor == nullptr)
    {
        return false;
    }
    ++cursor;

    clear_weather_daily_forecast(status);
    const float kMmPerUnit = precipitation_mm_per_unit(json);

    while (*cursor != '\0' && *cursor != ']' &&
           status.weather_daily_forecast_count < kWeatherDailyForecastEntryCount)
    {
        const char* object_start = std::strchr(cursor, '{');
        if (object_start == nullptr)
        {
            break;
        }

        const char* object_end = weather_json::find_matching_json_object_end(object_start);
        if (object_end == nullptr)
        {
            break;
        }

        char object_json[384] = {};
        const size_t object_len = static_cast<size_t>(object_end - object_start + 1);
        if (object_len >= sizeof(object_json))
        {
            cursor = object_end + 1;
            continue;
        }
        std::memcpy(object_json, object_start, object_len);
        object_json[object_len] = '\0';

        WeatherDailyForecastEntry& entry =
            status.weather_daily_forecast[status.weather_daily_forecast_count];

        char datetime_text[32] = {};
        char temperature_high_text[16] = {};
        char temperature_low_text[16] = {};
        char wind_speed_text[16] = {};
        char wind_bearing_text[16] = {};
        char condition_text[24] = {};

        if (!weather_json::extract_json_string_value(object_json, "\"datetime\":\"",
                                                      datetime_text, sizeof(datetime_text)) ||
            !weather_normalisation::format_forecast_date_text(datetime_text, entry.date_text.data(),
                                                              entry.date_text.size()))
        {
            cursor = object_end + 1;
            continue;
        }

        const bool have_temperature_high = weather_json::extract_json_scalar_value(
            object_json, "\"temperature\":", temperature_high_text,
            sizeof(temperature_high_text));
        const bool have_temperature_low = weather_json::extract_json_scalar_value(
            object_json, "\"templow\":", temperature_low_text, sizeof(temperature_low_text));
        if (!weather_normalisation::format_temperature_range_text(
                have_temperature_high ? temperature_high_text : nullptr,
                have_temperature_low ? temperature_low_text : nullptr, temperature_unit,
                entry.temperature_text.data(), entry.temperature_text.size()))
        {
            copy_text(entry.temperature_text, "-");
        }

        weather_extremes::Reading reading = {};
        float celsius = 0.0F;
        if (have_temperature_high && weather_normalisation::convert_temperature_to_celsius(
                                         temperature_high_text, temperature_unit, &celsius))
        {
            reading.high_celsius = celsius;
        }
        if (have_temperature_low && weather_normalisation::convert_temperature_to_celsius(
                                        temperature_low_text, temperature_unit, &celsius))
        {
            reading.low_celsius = celsius;
        }

        const bool have_wind_speed = weather_json::extract_json_scalar_value(
            object_json, "\"wind_speed\":", wind_speed_text, sizeof(wind_speed_text));
        float wind_speed_mph = 0.0F;
        if (have_wind_speed && weather_normalisation::convert_wind_speed_to_mph_value(
                                   wind_speed_text, wind_source_unit, &wind_speed_mph))
        {
            reading.wind_mph = wind_speed_mph;
        }
        const bool have_wind_bearing =
            weather_json::extract_json_string_value(object_json, "\"wind_bearing\":\"",
                                                     wind_bearing_text,
                                                     sizeof(wind_bearing_text)) ||
            weather_json::extract_json_scalar_value(object_json, "\"wind_bearing\":",
                                                     wind_bearing_text, sizeof(wind_bearing_text));
        if (!weather_normalisation::format_compact_wind_text(
                have_wind_speed ? wind_speed_text : nullptr,
                have_wind_bearing ? wind_bearing_text : nullptr, wind_source_unit,
                entry.wind_text.data(), entry.wind_text.size()))
        {
            copy_text(entry.wind_text, "-");
        }

        if (weather_json::extract_json_string_value(object_json, "\"condition\":\"",
                                                     condition_text, sizeof(condition_text)))
        {
            copy_text(entry.condition_text,
                      weather_normalisation::friendly_weather_condition(condition_text));
            record_weather_condition_warning(status, condition_text, " in the daily forecast");
        }
        else
        {
            copy_text(entry.condition_text, "?");
        }

        (void)add_precipitation_and_gust(object_json, condition_text, wind_source_unit, kMmPerUnit,
                                         reading);
        entry.hazards = weather_extremes::classify(reading, weather_extremes::Period::Day);

        ++status.weather_daily_forecast_count;
        cursor = object_end + 1;
    }

    return status.weather_daily_forecast_count > 0;
}

} // namespace weather_forecast
