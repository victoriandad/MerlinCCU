#include "home_assistant_weather_parser.h"

#include <cstdio>
#include <cstring>

#include "text_utils.h"
#include "weather_forecast_parser.h"
#include "weather_json_scan.h"
#include "weather_normalisation.h"

namespace home_assistant_weather
{

namespace
{
using text_utils::copy_text;
} // namespace

void update_weather_source_hint_from_json(const char* json, HomeAssistantStatus& status)
{
    char attribution[96] = {};
    char friendly_name[64] = {};
    char provider_name[64] = {};

    // Attribution is preferred because it usually points at the underlying
    // forecast provider, while the entity friendly name can be user-edited and
    // less useful as provenance text on the display.
    status.weather_source_hint.fill('\0');

    if (weather_json::extract_json_string_value(json, "\"attribution\":\"", attribution,
                                                sizeof(attribution)) &&
        weather_normalisation::normalize_weather_provider_name(attribution, provider_name,
                                                                sizeof(provider_name)))
    {
        copy_text(status.weather_source_hint, provider_name);
        return;
    }

    if (weather_json::extract_json_string_value(json, "\"friendly_name\":\"", friendly_name,
                                                sizeof(friendly_name)))
    {
        weather_normalisation::trim_text_in_place(friendly_name);
        if (friendly_name[0] != '\0')
        {
            copy_text(status.weather_source_hint, friendly_name);
        }
    }
}

void update_sun_times_from_json(const char* json, HomeAssistantStatus& status)
{
    char next_rising[40] = {};
    char next_setting[40] = {};

    status.sunrise_text.fill('\0');
    status.sunset_text.fill('\0');

    if (weather_json::extract_json_string_value(json, "\"next_rising\":\"", next_rising,
                                                sizeof(next_rising)))
    {
        weather_normalisation::format_hour_text(next_rising, status.sunrise_text.data(),
                                                status.sunrise_text.size());
    }

    if (weather_json::extract_json_string_value(json, "\"next_setting\":\"", next_setting,
                                                sizeof(next_setting)))
    {
        weather_normalisation::format_hour_text(next_setting, status.sunset_text.data(),
                                                status.sunset_text.size());
    }
}

bool parse_current_weather_entity(const char* json, HomeAssistantStatus& status,
                                  char& temperature_unit, char* wind_source_unit,
                                  size_t wind_source_unit_size)
{
    char raw_condition[24] = {};
    char raw_temperature[12] = {};
    char raw_unit[8] = {};
    char raw_wind_speed[16] = {};
    char raw_wind_unit[16] = {};

    weather_forecast::clear_current_weather_metrics(status);
    weather_forecast::clear_weather_alert_status(status);
    temperature_unit = '\0';
    if (wind_source_unit != nullptr && wind_source_unit_size > 0)
    {
        wind_source_unit[0] = '\0';
    }
    update_weather_source_hint_from_json(json, status);

    const bool have_state = weather_json::extract_json_string_value(
        json, "\"state\":\"", raw_condition, sizeof(raw_condition));
    if (have_state)
    {
        copy_text(status.weather_condition,
                  weather_normalisation::friendly_weather_condition(raw_condition));
        weather_forecast::record_weather_condition_warning(status, raw_condition, " now");
    }
    else
    {
        copy_text(status.weather_condition, "?");
    }

    if (weather_json::extract_json_scalar_value(json, "\"temperature\":", raw_temperature,
                                                sizeof(raw_temperature)))
    {
        if (weather_json::extract_json_string_value(json, "\"temperature_unit\":\"", raw_unit,
                                                    sizeof(raw_unit)))
        {
            const char unit = weather_normalisation::normalized_temperature_unit(raw_unit);
            temperature_unit = unit;
            if (unit != '\0')
            {
                float temperature_celsius = 0.0F;
                if (weather_normalisation::convert_temperature_to_celsius(raw_temperature, unit,
                                                                          &temperature_celsius))
                {
                    status.weather_metrics.current_temperature_celsius = temperature_celsius;
                    status.weather_metrics.current_temperature_celsius_valid = true;
                }

                char formatted_temperature[sizeof(status.weather_temperature)] = {};
                std::snprintf(formatted_temperature, sizeof(formatted_temperature), "%s %c",
                              raw_temperature, unit);
                copy_text(status.weather_temperature, formatted_temperature);
            }
            else
            {
                copy_text(status.weather_temperature, raw_temperature);
            }
        }
        else
        {
            copy_text(status.weather_temperature, raw_temperature);
        }
    }
    else
    {
        status.weather_temperature.fill('\0');
    }

    if (weather_json::extract_json_string_value(json, "\"wind_speed_unit\":\"", raw_wind_unit,
                                                sizeof(raw_wind_unit)) &&
        wind_source_unit != nullptr &&
        weather_normalisation::normalize_wind_speed_unit(raw_wind_unit, wind_source_unit,
                                                          wind_source_unit_size))
    {
        copy_text(status.weather_wind_unit, "mph");
    }
    else
    {
        if (wind_source_unit != nullptr && wind_source_unit_size > 0)
        {
            wind_source_unit[0] = '\0';
        }
        status.weather_wind_unit.fill('\0');
    }

    if (weather_json::extract_json_scalar_value(json, "\"wind_speed\":", raw_wind_speed,
                                                sizeof(raw_wind_speed)))
    {
        float wind_speed_mph = 0.0F;
        if (weather_normalisation::convert_wind_speed_to_mph_value(
                raw_wind_speed, wind_source_unit, &wind_speed_mph))
        {
            status.weather_metrics.current_wind_speed_mph = wind_speed_mph;
            status.weather_metrics.current_wind_speed_mph_valid = true;
        }
    }

    return have_state;
}

} // namespace home_assistant_weather
