#include "open_meteo_parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "text_utils.h"
#include "weather_forecast_parser.h"
#include "weather_json_scan.h"
#include "weather_normalisation.h"

namespace open_meteo
{

namespace
{
using text_utils::copy_text;
} // namespace

bool parse_weather(const char* json, HomeAssistantStatus& status, char& temperature_unit,
                   char* wind_source_unit, size_t wind_source_unit_size)
{
    if (json == nullptr)
    {
        return false;
    }

    weather_forecast::clear_current_weather_metrics(status);
    weather_forecast::clear_weather_alert_status(status);
    temperature_unit = 'C';
    if (wind_source_unit != nullptr && wind_source_unit_size > 0)
    {
        std::snprintf(wind_source_unit, wind_source_unit_size, "%s", "mph");
    }
    copy_text(status.weather_wind_unit, "mph");

    const char* current_section = std::strstr(json, "\"current\":{");
    if (current_section == nullptr)
    {
        current_section = std::strstr(json, "\"current_weather\":{");
    }

    char temperature[16] = {};
    if (current_section != nullptr &&
        weather_json::extract_json_scalar_value(current_section, "\"temperature_2m\":",
                                                 temperature, sizeof(temperature)))
    {
        float temperature_celsius = 0.0F;
        if (weather_normalisation::convert_temperature_to_celsius(temperature, temperature_unit,
                                                                   &temperature_celsius))
        {
            status.weather_metrics.current_temperature_celsius = temperature_celsius;
            status.weather_metrics.current_temperature_celsius_valid = true;
        }

        char formatted_temperature[sizeof(status.weather_temperature)] = {};
        std::snprintf(formatted_temperature, sizeof(formatted_temperature), "%s C", temperature);
        copy_text(status.weather_temperature, formatted_temperature);
    }
    else
    {
        status.weather_temperature.fill('\0');
    }

    char weather_code_text[12] = {};
    if (current_section != nullptr &&
        weather_json::extract_json_scalar_value(current_section, "\"weather_code\":",
                                                 weather_code_text, sizeof(weather_code_text)))
    {
        const char* condition =
            weather_normalisation::open_meteo_condition_from_code(std::atoi(weather_code_text));
        copy_text(status.weather_condition, condition);
        weather_forecast::record_weather_condition_warning(status, condition, " now");
    }
    else
    {
        copy_text(status.weather_condition, "Weather");
    }

    char current_wind_speed_text[16] = {};
    if (current_section != nullptr &&
        (weather_json::extract_json_scalar_value(current_section, "\"wind_speed_10m\":",
                                                  current_wind_speed_text,
                                                  sizeof(current_wind_speed_text)) ||
         weather_json::extract_json_scalar_value(current_section, "\"windspeed\":",
                                                  current_wind_speed_text,
                                                  sizeof(current_wind_speed_text))))
    {
        float current_wind_speed_mph = 0.0F;
        if (weather_normalisation::convert_wind_speed_to_mph_value(
                current_wind_speed_text, wind_source_unit, &current_wind_speed_mph))
        {
            status.weather_metrics.current_wind_speed_mph = current_wind_speed_mph;
            status.weather_metrics.current_wind_speed_mph_valid = true;
        }
    }

    const char* hourly_time_array =
        weather_json::find_json_array_start(json, "\"hourly\":{\"time\":");
    const char* hourly_temp_array =
        weather_json::find_json_array_start(json, "\"hourly\":{\"time\":");
    const char* hourly_wind_array =
        weather_json::find_json_array_start(json, "\"hourly\":{\"time\":");
    const char* hourly_direction_array =
        weather_json::find_json_array_start(json, "\"hourly\":{\"time\":");
    const char* hourly_code_array =
        weather_json::find_json_array_start(json, "\"hourly\":{\"time\":");
    if (hourly_temp_array != nullptr)
    {
        hourly_temp_array =
            weather_json::find_json_array_start(hourly_temp_array, "\"temperature_2m\":");
    }
    if (hourly_wind_array != nullptr)
    {
        hourly_wind_array =
            weather_json::find_json_array_start(hourly_wind_array, "\"wind_speed_10m\":");
    }
    if (hourly_code_array != nullptr)
    {
        hourly_code_array =
            weather_json::find_json_array_start(hourly_code_array, "\"weather_code\":");
    }
    if (hourly_direction_array != nullptr)
    {
        hourly_direction_array =
            weather_json::find_json_array_start(hourly_direction_array, "\"wind_direction_10m\":");
    }
    if (hourly_time_array == nullptr || hourly_temp_array == nullptr ||
        hourly_wind_array == nullptr || hourly_direction_array == nullptr ||
        hourly_code_array == nullptr)
    {
        weather_forecast::clear_weather_forecast(status);
        weather_forecast::clear_weather_daily_forecast(status);
        return false;
    }

    weather_forecast::clear_weather_forecast(status);
    for (size_t i = 0; i < kWeatherForecastEntryCount; ++i)
    {
        char time_iso[32] = {};
        char temperature_text[16] = {};
        char wind_text[16] = {};
        char direction_text[16] = {};
        char code_text[12] = {};
        if (!weather_json::json_array_string_at(hourly_time_array, i, time_iso,
                                                sizeof(time_iso)) ||
            !weather_json::json_array_number_at(hourly_temp_array, i, temperature_text,
                                                sizeof(temperature_text)) ||
            !weather_json::json_array_number_at(hourly_wind_array, i, wind_text,
                                                sizeof(wind_text)) ||
            !weather_json::json_array_number_at(hourly_direction_array, i, direction_text,
                                                sizeof(direction_text)) ||
            !weather_json::json_array_number_at(hourly_code_array, i, code_text,
                                                sizeof(code_text)))
        {
            break;
        }

        WeatherForecastEntry& entry = status.weather_forecast[status.weather_forecast_count];
        if (!weather_normalisation::format_hour_text(time_iso, entry.time_text.data(),
                                                      entry.time_text.size()))
        {
            continue;
        }

        float temperature_celsius = 0.0F;
        if (weather_normalisation::convert_temperature_to_celsius(
                temperature_text, temperature_unit, &temperature_celsius))
        {
            weather_forecast::include_forecast_temperature_celsius(status, temperature_celsius);
        }

        std::snprintf(entry.temperature_text.data(), entry.temperature_text.size(), "%s C",
                      temperature_text);
        float wind_speed_mph = 0.0F;
        if (weather_normalisation::convert_wind_speed_to_mph_value(wind_text, wind_source_unit,
                                                                    &wind_speed_mph))
        {
            weather_forecast::include_forecast_wind_speed_mph(status, wind_speed_mph);
        }

        if (!weather_normalisation::format_compact_wind_text(
                wind_text, direction_text, wind_source_unit, entry.wind_text.data(),
                entry.wind_text.size()))
        {
            copy_text(entry.wind_text, "-");
        }
        const char* condition =
            weather_normalisation::open_meteo_condition_from_code(std::atoi(code_text));
        copy_text(entry.condition_text, condition);
        weather_forecast::record_weather_condition_warning(status, condition,
                                                            " in the hourly forecast");
        ++status.weather_forecast_count;
    }

    const char* daily_section = std::strstr(json, "\"daily\":{");
    const char* sunrise_array = weather_json::find_json_array_start(daily_section, "\"sunrise\":");
    const char* sunset_array = weather_json::find_json_array_start(daily_section, "\"sunset\":");
    char sunrise_iso[32] = {};
    char sunset_iso[32] = {};
    status.sunrise_text.fill('\0');
    status.sunset_text.fill('\0');
    if (sunrise_array != nullptr &&
        weather_json::json_array_string_at(sunrise_array, 0, sunrise_iso, sizeof(sunrise_iso)))
    {
        weather_normalisation::format_hour_text(sunrise_iso, status.sunrise_text.data(),
                                                status.sunrise_text.size());
    }
    if (sunset_array != nullptr &&
        weather_json::json_array_string_at(sunset_array, 0, sunset_iso, sizeof(sunset_iso)))
    {
        weather_normalisation::format_hour_text(sunset_iso, status.sunset_text.data(),
                                                status.sunset_text.size());
    }

    weather_forecast::clear_weather_daily_forecast(status);
    if (daily_section != nullptr)
    {
        for (size_t i = 0; i < kWeatherDailyForecastEntryCount; ++i)
        {
            char date_iso[32] = {};
            char temperature_max_text[16] = {};
            char temperature_min_text[16] = {};
            char wind_max_text[16] = {};
            char wind_direction_text[16] = {};
            char code_text[12] = {};
            if (!weather_json::open_meteo_daily_scalar(daily_section, "\"time\":", nullptr, i,
                                                       date_iso, sizeof(date_iso), true) ||
                !weather_json::open_meteo_daily_scalar(
                    daily_section, "\"temperature_2m_max\":", nullptr, i, temperature_max_text,
                    sizeof(temperature_max_text), false) ||
                !weather_json::open_meteo_daily_scalar(
                    daily_section, "\"temperature_2m_min\":", nullptr, i, temperature_min_text,
                    sizeof(temperature_min_text), false) ||
                !weather_json::open_meteo_daily_scalar(
                    daily_section, "\"wind_speed_10m_max\":", "\"windspeed_10m_max\":", i,
                    wind_max_text, sizeof(wind_max_text), false) ||
                !weather_json::open_meteo_daily_scalar(
                    daily_section, "\"wind_direction_10m_dominant\":",
                    "\"winddirection_10m_dominant\":", i, wind_direction_text,
                    sizeof(wind_direction_text), false) ||
                !weather_json::open_meteo_daily_scalar(daily_section, "\"weather_code\":",
                                                       "\"weathercode\":", i, code_text,
                                                       sizeof(code_text), false))
            {
                break;
            }

            WeatherDailyForecastEntry& entry =
                status.weather_daily_forecast[status.weather_daily_forecast_count];
            if (!weather_normalisation::format_forecast_date_text(date_iso, entry.date_text.data(),
                                                                  entry.date_text.size()))
            {
                continue;
            }

            if (!weather_normalisation::format_temperature_range_text(
                    temperature_max_text, temperature_min_text, 'C', entry.temperature_text.data(),
                    entry.temperature_text.size()))
            {
                copy_text(entry.temperature_text, "-");
            }

            if (!weather_normalisation::format_compact_wind_text(
                    wind_max_text, wind_direction_text, wind_source_unit, entry.wind_text.data(),
                    entry.wind_text.size()))
            {
                copy_text(entry.wind_text, "-");
            }

            const char* condition =
                weather_normalisation::open_meteo_condition_from_code(std::atoi(code_text));
            copy_text(entry.condition_text, condition);
            weather_forecast::record_weather_condition_warning(status, condition,
                                                                " in the daily forecast");
            ++status.weather_daily_forecast_count;
        }
    }

    return status.weather_forecast_count > 0;
}

} // namespace open_meteo
