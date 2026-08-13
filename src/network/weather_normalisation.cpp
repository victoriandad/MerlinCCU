#include "weather_normalisation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "time_manager.h"

namespace weather_normalisation
{

namespace
{

/// @brief Returns whether a string starts with a prefix ignoring ASCII case.
bool starts_with_ignore_case(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr)
    {
        return false;
    }

    while (*prefix != '\0')
    {
        if (*text == '\0')
        {
            return false;
        }

        if (std::tolower(static_cast<unsigned char>(*text)) !=
            std::tolower(static_cast<unsigned char>(*prefix)))
        {
            return false;
        }

        ++text;
        ++prefix;
    }

    return true;
}

} // namespace

void trim_text_in_place(char* text)
{
    if (text == nullptr)
    {
        return;
    }

    size_t start = 0;
    const size_t length = std::strlen(text);
    while (text[start] != '\0' && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }

    size_t end = length;
    while (end > start &&
           (std::isspace(static_cast<unsigned char>(text[end - 1])) || text[end - 1] == '.'))
    {
        --end;
    }

    if (start >= end)
    {
        text[0] = '\0';
        return;
    }

    if (start > 0)
    {
        std::memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

bool normalize_weather_provider_name(const char* raw_text, char* out, size_t out_size)
{
    if (raw_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* provider = raw_text;
    constexpr const char* kPrefixes[] = {
        "Data provided by ", "Weather forecast from ", "Forecast provided by ", "Provided by ",
        "Powered by ",
    };

    for (const char* prefix : kPrefixes)
    {
        if (starts_with_ignore_case(provider, prefix))
        {
            provider += std::strlen(prefix);
            break;
        }
    }

    std::snprintf(out, out_size, "%s", provider);
    trim_text_in_place(out);
    return out[0] != '\0';
}

bool normalize_wind_speed_unit(const char* unit_text, char* out, size_t out_size)
{
    if (unit_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    if (std::strstr(unit_text, "km") != nullptr || std::strstr(unit_text, "KM") != nullptr)
    {
        std::snprintf(out, out_size, "km/h");
        return true;
    }
    if (std::strstr(unit_text, "m/s") != nullptr || std::strstr(unit_text, "M/S") != nullptr)
    {
        std::snprintf(out, out_size, "m/s");
        return true;
    }
    if (std::strstr(unit_text, "mi") != nullptr || std::strstr(unit_text, "MI") != nullptr)
    {
        std::snprintf(out, out_size, "mph");
        return true;
    }
    if (std::strstr(unit_text, "ft/s") != nullptr || std::strstr(unit_text, "FT/S") != nullptr)
    {
        std::snprintf(out, out_size, "ft/s");
        return true;
    }
    if (std::strstr(unit_text, "Beaufort") != nullptr ||
        std::strstr(unit_text, "beaufort") != nullptr)
    {
        std::snprintf(out, out_size, "Bft");
        return true;
    }
    if (std::strstr(unit_text, "kn") != nullptr || std::strstr(unit_text, "KN") != nullptr)
    {
        std::snprintf(out, out_size, "kn");
        return true;
    }

    std::snprintf(out, out_size, "%s", unit_text);
    return out[0] != '\0';
}

bool format_compact_scalar_value(const char* scalar_text, char* out, size_t out_size)
{
    if (scalar_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char* parse_end = nullptr;
    const float value = std::strtof(scalar_text, &parse_end);
    if (parse_end != scalar_text && parse_end != nullptr && *parse_end == '\0')
    {
        const int rounded = static_cast<int>(std::lround(value));
        std::snprintf(out, out_size, "%d", rounded);
        return true;
    }

    std::snprintf(out, out_size, "%s", scalar_text);
    return out[0] != '\0';
}

bool parse_float_value(const char* value_text, float* out)
{
    if (value_text == nullptr || out == nullptr)
    {
        return false;
    }

    char* parse_end = nullptr;
    const float value = std::strtof(value_text, &parse_end);
    if (parse_end == value_text || parse_end == nullptr)
    {
        return false;
    }

    while (*parse_end != '\0')
    {
        if (!std::isspace(static_cast<unsigned char>(*parse_end)))
        {
            return false;
        }
        ++parse_end;
    }

    *out = value;
    return true;
}

bool convert_temperature_to_celsius(const char* temperature_text, char source_unit, float* out)
{
    float raw_value = 0.0F;
    if (!parse_float_value(temperature_text, &raw_value) || out == nullptr)
    {
        return false;
    }

    if (source_unit == 'C' || source_unit == 'c')
    {
        *out = raw_value;
        return true;
    }
    if (source_unit == 'F' || source_unit == 'f')
    {
        *out = (raw_value - 32.0F) * (5.0F / 9.0F);
        return true;
    }

    return false;
}

bool convert_wind_speed_to_mph_value(const char* speed_text, const char* source_unit, float* out)
{
    float raw_value = 0.0F;
    if (!parse_float_value(speed_text, &raw_value) || out == nullptr)
    {
        return false;
    }

    float mph_value = raw_value;
    if (source_unit != nullptr && source_unit[0] != '\0')
    {
        if (std::strcmp(source_unit, "km/h") == 0)
        {
            mph_value = raw_value * 0.621371F;
        }
        else if (std::strcmp(source_unit, "m/s") == 0)
        {
            mph_value = raw_value * 2.23694F;
        }
        else if (std::strcmp(source_unit, "ft/s") == 0)
        {
            mph_value = raw_value * 0.681818F;
        }
        else if (std::strcmp(source_unit, "kn") == 0)
        {
            mph_value = raw_value * 1.15078F;
        }
        else if (std::strcmp(source_unit, "Bft") == 0)
        {
            static constexpr float kBeaufortToMph[] = {
                0.0F,  1.0F,  4.0F,  8.0F,  13.0F, 19.0F, 25.0F,
                32.0F, 39.0F, 47.0F, 55.0F, 64.0F, 73.0F, 83.0F,
            };
            const int index = std::clamp(
                static_cast<int>(std::lround(raw_value)), 0,
                static_cast<int>((sizeof(kBeaufortToMph) / sizeof(kBeaufortToMph[0])) - 1));
            mph_value = kBeaufortToMph[index];
        }
    }

    *out = mph_value;
    return true;
}

bool convert_wind_speed_to_mph(const char* speed_text, const char* source_unit, char* out,
                               size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    float mph_value = 0.0F;
    if (!convert_wind_speed_to_mph_value(speed_text, source_unit, &mph_value))
    {
        return false;
    }

    const int rounded = static_cast<int>(std::lround(mph_value));
    std::snprintf(out, out_size, "%d", rounded);
    return true;
}

bool format_wind_direction_text(const char* bearing_text, char* out, size_t out_size)
{
    if (bearing_text == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char* parse_end = nullptr;
    float bearing = std::strtof(bearing_text, &parse_end);
    if (parse_end != bearing_text && parse_end != nullptr && *parse_end == '\0')
    {
        while (bearing < 0.0F)
        {
            bearing += 360.0F;
        }
        while (bearing >= 360.0F)
        {
            bearing -= 360.0F;
        }

        static constexpr const char* kCompass16[] = {
            "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
        };
        const int index = static_cast<int>((bearing + 11.25F) / 22.5F) % 16;
        std::snprintf(out, out_size, "%s", kCompass16[index]);
        return true;
    }

    size_t out_index = 0;
    for (size_t i = 0; bearing_text[i] != '\0' && out_index + 1 < out_size; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(bearing_text[i]);
        if (std::isspace(ch))
        {
            continue;
        }
        out[out_index++] = static_cast<char>(std::toupper(ch));
    }
    out[out_index] = '\0';
    return out_index > 0;
}

bool format_compact_wind_text(const char* speed_text, const char* bearing_text,
                              const char* wind_source_unit, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char compact_speed[8] = {};
    char compact_direction[4] = {};
    const bool have_speed = speed_text != nullptr &&
                            convert_wind_speed_to_mph(speed_text, wind_source_unit, compact_speed,
                                                      sizeof(compact_speed)) &&
                            compact_speed[0] != '\0';
    const bool have_direction =
        bearing_text != nullptr &&
        format_wind_direction_text(bearing_text, compact_direction, sizeof(compact_direction)) &&
        compact_direction[0] != '\0';

    if (have_speed && have_direction)
    {
        std::snprintf(out, out_size, "%s %s", compact_speed, compact_direction);
        return true;
    }
    if (have_speed)
    {
        std::snprintf(out, out_size, "%s", compact_speed);
        return true;
    }
    if (have_direction)
    {
        std::snprintf(out, out_size, "%s", compact_direction);
        return true;
    }

    return false;
}

bool weather_condition_is_warning(const char* condition_text)
{
    if (condition_text == nullptr)
    {
        return false;
    }

    char normalized[32] = {};
    size_t out_index = 0;
    for (size_t i = 0; condition_text[i] != '\0' && out_index + 1 < sizeof(normalized); ++i)
    {
        normalized[out_index++] =
            static_cast<char>(std::tolower(static_cast<unsigned char>(condition_text[i])));
    }
    normalized[out_index] = '\0';

    return std::strstr(normalized, "lightning") != nullptr ||
           std::strstr(normalized, "thunder") != nullptr ||
           std::strstr(normalized, "hail") != nullptr ||
           std::strstr(normalized, "tornado") != nullptr ||
           std::strstr(normalized, "hurricane") != nullptr;
}

const char* friendly_weather_condition(const char* raw_condition)
{
    if (raw_condition == nullptr || raw_condition[0] == '\0')
    {
        return "";
    }

    if (std::strcmp(raw_condition, "clear-night") == 0)
    {
        return "CLEAR NIGHT";
    }
    if (std::strcmp(raw_condition, "cloudy") == 0)
    {
        return "CLOUDY";
    }
    if (std::strcmp(raw_condition, "exceptional") == 0)
    {
        return "EXCEPTIONAL";
    }
    if (std::strcmp(raw_condition, "fog") == 0)
    {
        return "FOG";
    }
    if (std::strcmp(raw_condition, "hail") == 0)
    {
        return "HAIL";
    }
    if (std::strcmp(raw_condition, "lightning") == 0)
    {
        return "LIGHTNING";
    }
    if (std::strcmp(raw_condition, "lightning-rainy") == 0)
    {
        return "LTNG RAIN";
    }
    if (std::strcmp(raw_condition, "partlycloudy") == 0)
    {
        return "PARTLY CLOUDY";
    }
    if (std::strcmp(raw_condition, "pouring") == 0)
    {
        return "POURING";
    }
    if (std::strcmp(raw_condition, "rainy") == 0)
    {
        return "RAIN";
    }
    if (std::strcmp(raw_condition, "snowy") == 0)
    {
        return "SNOW";
    }
    if (std::strcmp(raw_condition, "snowy-rainy") == 0)
    {
        return "SLEET";
    }
    if (std::strcmp(raw_condition, "sunny") == 0)
    {
        return "SUNNY";
    }
    if (std::strcmp(raw_condition, "windy") == 0)
    {
        return "WINDY";
    }
    if (std::strcmp(raw_condition, "windy-variant") == 0)
    {
        return "BREEZY";
    }
    if (std::strcmp(raw_condition, "unavailable") == 0)
    {
        return "UNAVAILABLE";
    }

    return raw_condition;
}

const char* open_meteo_condition_from_code(int code)
{
    switch (code)
    {
    case 0:
        return "Clear";
    case 1:
    case 2:
    case 3:
        return "Cloudy";
    case 45:
    case 48:
        return "Fog";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
        return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
        return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
        return "Snow";
    case 80:
    case 81:
    case 82:
        return "Rain showers";
    case 85:
    case 86:
        return "Snow showers";
    case 95:
    case 96:
    case 99:
        return "Thunder";
    default:
        return "Weather";
    }
}

char normalized_temperature_unit(const char* unit_text)
{
    if (unit_text == nullptr)
    {
        return '\0';
    }

    for (const char* p = unit_text; *p != '\0'; ++p)
    {
        if (*p == 'C' || *p == 'c')
        {
            return 'C';
        }
        if (*p == 'F' || *p == 'f')
        {
            return 'F';
        }
    }

    return '\0';
}

bool format_hour_text(const char* iso_datetime, char* out, size_t out_size)
{
    if (time_manager::format_local_time_from_iso8601(iso_datetime, out, out_size))
    {
        return true;
    }

    if (iso_datetime == nullptr || out == nullptr || out_size < 6)
    {
        return false;
    }

    const char* time_sep = std::strchr(iso_datetime, 'T');
    if (time_sep == nullptr || std::strlen(time_sep + 1) < 5)
    {
        return false;
    }

    std::snprintf(out, out_size, "%.5s", time_sep + 1);
    return true;
}

bool format_forecast_date_text(const char* iso_datetime, char* out, size_t out_size)
{
    if (iso_datetime == nullptr || out == nullptr || out_size < 11)
    {
        return false;
    }

    if (!(std::isdigit(static_cast<unsigned char>(iso_datetime[0])) &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[1])) &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[2])) &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[3])) && iso_datetime[4] == '-' &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[5])) &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[6])) && iso_datetime[7] == '-' &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[8])) &&
          std::isdigit(static_cast<unsigned char>(iso_datetime[9]))))
    {
        return false;
    }

    std::snprintf(out, out_size, "%.10s", iso_datetime);
    return true;
}

bool format_temperature_range_text(const char* high_text, const char* low_text, char unit,
                                   char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    char compact_high[8] = {};
    char compact_low[8] = {};
    const bool have_high =
        high_text != nullptr &&
        format_compact_scalar_value(high_text, compact_high, sizeof(compact_high));
    const bool have_low = low_text != nullptr &&
                          format_compact_scalar_value(low_text, compact_low, sizeof(compact_low));

    if (have_low && have_high)
    {
        if (unit != '\0')
        {
            std::snprintf(out, out_size, "%s-%s%c", compact_low, compact_high, unit);
        }
        else
        {
            std::snprintf(out, out_size, "%s-%s", compact_low, compact_high);
        }
        return true;
    }

    if (have_high)
    {
        if (unit != '\0')
        {
            std::snprintf(out, out_size, "%s%c", compact_high, unit);
        }
        else
        {
            std::snprintf(out, out_size, "%s", compact_high);
        }
        return true;
    }

    return false;
}

} // namespace weather_normalisation
