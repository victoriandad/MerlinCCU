#include "weather_normalisation.h"

#include <cmath>
#include <cstring>

#include "test_framework.h"

HOST_TEST(normalize_weather_provider_name_strips_known_prefixes)
{
    char out[64] = {};
    EXPECT_TRUE(weather_normalisation::normalize_weather_provider_name(
        "Weather forecast from Open-Meteo.com", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "Open-Meteo.com") == 0);
}

HOST_TEST(normalize_weather_provider_name_trims_trailing_period)
{
    char out[64] = {};
    EXPECT_TRUE(
        weather_normalisation::normalize_weather_provider_name("Provided by Met.no.", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "Met.no") == 0);
}

HOST_TEST(normalize_wind_speed_unit_maps_common_labels)
{
    char out[8] = {};
    EXPECT_TRUE(weather_normalisation::normalize_wind_speed_unit("km/h", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "km/h") == 0);

    EXPECT_TRUE(weather_normalisation::normalize_wind_speed_unit("Beaufort", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "Bft") == 0);
}

HOST_TEST(format_compact_scalar_value_rounds_numeric_text)
{
    char out[8] = {};
    EXPECT_TRUE(weather_normalisation::format_compact_scalar_value("12.6", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "13") == 0);
}

HOST_TEST(format_compact_scalar_value_passes_through_non_numeric_text)
{
    char out[8] = {};
    EXPECT_TRUE(weather_normalisation::format_compact_scalar_value("N/A", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "N/A") == 0);
}

HOST_TEST(convert_temperature_to_celsius_converts_fahrenheit)
{
    float celsius = 0.0F;
    EXPECT_TRUE(weather_normalisation::convert_temperature_to_celsius("32", 'F', &celsius));
    EXPECT_TRUE(std::fabs(celsius) < 0.01F);
}

HOST_TEST(convert_temperature_to_celsius_rejects_unknown_unit)
{
    float celsius = 0.0F;
    EXPECT_FALSE(weather_normalisation::convert_temperature_to_celsius("32", 'K', &celsius));
}

HOST_TEST(convert_wind_speed_to_mph_value_converts_km_per_hour)
{
    float mph = 0.0F;
    EXPECT_TRUE(weather_normalisation::convert_wind_speed_to_mph_value("100", "km/h", &mph));
    EXPECT_TRUE(std::fabs(mph - 62.1371F) < 0.01F);
}

HOST_TEST(convert_wind_speed_to_mph_value_treats_empty_unit_as_already_mph)
{
    float mph = 0.0F;
    EXPECT_TRUE(weather_normalisation::convert_wind_speed_to_mph_value("15", "", &mph));
    EXPECT_TRUE(std::fabs(mph - 15.0F) < 0.01F);
}

HOST_TEST(convert_wind_speed_to_mph_value_maps_beaufort_scale)
{
    float mph = 0.0F;
    EXPECT_TRUE(weather_normalisation::convert_wind_speed_to_mph_value("4", "Bft", &mph));
    EXPECT_TRUE(std::fabs(mph - 13.0F) < 0.01F);
}

HOST_TEST(format_wind_direction_text_maps_bearing_to_compass_point)
{
    char out[8] = {};
    EXPECT_TRUE(weather_normalisation::format_wind_direction_text("0", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "N") == 0);

    EXPECT_TRUE(weather_normalisation::format_wind_direction_text("90", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "E") == 0);
}

HOST_TEST(format_compact_wind_text_combines_speed_and_direction)
{
    char out[16] = {};
    EXPECT_TRUE(weather_normalisation::format_compact_wind_text("10", "180", "km/h", out,
                                                                 sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "6 S") == 0);
}

HOST_TEST(format_compact_wind_text_returns_false_with_no_inputs)
{
    char out[16] = {};
    EXPECT_FALSE(
        weather_normalisation::format_compact_wind_text(nullptr, nullptr, "mph", out, sizeof(out)));
}

HOST_TEST(weather_condition_is_warning_flags_severe_conditions)
{
    EXPECT_TRUE(weather_normalisation::weather_condition_is_warning("lightning-rainy"));
    EXPECT_TRUE(weather_normalisation::weather_condition_is_warning("Thunder"));
    EXPECT_FALSE(weather_normalisation::weather_condition_is_warning("cloudy"));
}

HOST_TEST(friendly_weather_condition_maps_known_ha_codes)
{
    EXPECT_TRUE(std::strcmp(weather_normalisation::friendly_weather_condition("partlycloudy"),
                            "PARTLY CLOUDY") == 0);
}

HOST_TEST(friendly_weather_condition_passes_through_unknown_codes)
{
    EXPECT_TRUE(
        std::strcmp(weather_normalisation::friendly_weather_condition("mystery"), "mystery") == 0);
}

HOST_TEST(open_meteo_condition_from_code_maps_known_ranges)
{
    EXPECT_TRUE(std::strcmp(weather_normalisation::open_meteo_condition_from_code(0), "Clear") == 0);
    EXPECT_TRUE(std::strcmp(weather_normalisation::open_meteo_condition_from_code(65), "Rain") == 0);
    EXPECT_TRUE(
        std::strcmp(weather_normalisation::open_meteo_condition_from_code(999), "Weather") == 0);
}

HOST_TEST(normalized_temperature_unit_extracts_c_or_f)
{
    EXPECT_EQ(weather_normalisation::normalized_temperature_unit("°C"), 'C');
    EXPECT_EQ(weather_normalisation::normalized_temperature_unit("degF"), 'F');
    EXPECT_EQ(weather_normalisation::normalized_temperature_unit("kelvin"), '\0');
}

HOST_TEST(format_hour_text_falls_back_to_raw_utc_extraction)
{
    // The host build's time_manager stub always reports "not converted" (see
    // tests/host/stubs/time_manager_stub.cpp), so this exercises the raw
    // ISO-datetime fallback path directly.
    char out[6] = {};
    EXPECT_TRUE(weather_normalisation::format_hour_text("2026-01-01T14:30:00Z", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "14:30") == 0);
}

HOST_TEST(format_hour_text_returns_false_without_a_time_component)
{
    char out[6] = {};
    EXPECT_FALSE(weather_normalisation::format_hour_text("2026-01-01", out, sizeof(out)));
}

HOST_TEST(format_forecast_date_text_extracts_the_date_prefix)
{
    char out[11] = {};
    EXPECT_TRUE(
        weather_normalisation::format_forecast_date_text("2026-03-05T00:00:00", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "2026-03-05") == 0);
}

HOST_TEST(format_forecast_date_text_rejects_malformed_dates)
{
    char out[11] = {};
    EXPECT_FALSE(weather_normalisation::format_forecast_date_text("not-a-date", out, sizeof(out)));
}

HOST_TEST(format_temperature_range_text_combines_high_and_low_with_unit)
{
    char out[16] = {};
    EXPECT_TRUE(weather_normalisation::format_temperature_range_text("12.4", "5.1", 'C', out,
                                                                      sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "5-12C") == 0);
}

HOST_TEST(format_temperature_range_text_falls_back_to_high_only)
{
    char out[16] = {};
    EXPECT_TRUE(
        weather_normalisation::format_temperature_range_text("12.4", nullptr, 'C', out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "12C") == 0);
}
