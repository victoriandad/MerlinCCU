#include "open_meteo_parser.h"

#include <cstring>

#include "test_framework.h"

namespace
{
const char* kSampleResponse =
    "{\"current\":{\"temperature_2m\":15.2,\"weather_code\":3,\"wind_speed_10m\":12.0},"
    "\"hourly\":{\"time\":[\"2026-01-01T00:00\",\"2026-01-01T01:00\"],"
    "\"temperature_2m\":[10.0,11.0],\"wind_speed_10m\":[5.0,6.0],"
    "\"wind_direction_10m\":[90,180],\"weather_code\":[1,61]},"
    "\"daily\":{\"time\":[\"2026-01-01\"],\"temperature_2m_max\":[15.0],"
    "\"temperature_2m_min\":[5.0],\"wind_speed_10m_max\":[20.0],"
    "\"wind_direction_10m_dominant\":[270],\"weather_code\":[3],"
    "\"sunrise\":[\"2026-01-01T07:00\"],\"sunset\":[\"2026-01-01T17:00\"]}}";
} // namespace

HOST_TEST(parse_weather_populates_current_conditions)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    const bool ok = open_meteo::parse_weather(kSampleResponse, status, temperature_unit,
                                              wind_source_unit, sizeof(wind_source_unit));

    EXPECT_TRUE(ok);
    EXPECT_EQ(temperature_unit, 'C');
    EXPECT_TRUE(std::strcmp(wind_source_unit, "mph") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_temperature.data(), "15.2 C") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_condition.data(), "Cloudy") == 0);
    EXPECT_TRUE(status.weather_metrics.current_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.current_wind_speed_mph_valid);
    EXPECT_TRUE(status.weather_metrics.current_wind_speed_mph == 12.0F);
}

HOST_TEST(parse_weather_populates_hourly_forecast_rows)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    open_meteo::parse_weather(kSampleResponse, status, temperature_unit, wind_source_unit,
                              sizeof(wind_source_unit));

    EXPECT_EQ(status.weather_forecast_count, 2);
    EXPECT_TRUE(std::strcmp(status.weather_forecast[0].time_text.data(), "00:00") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_forecast[0].condition_text.data(), "Cloudy") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_forecast[1].condition_text.data(), "Rain") == 0);
}

HOST_TEST(parse_weather_populates_daily_forecast_and_sun_times)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    open_meteo::parse_weather(kSampleResponse, status, temperature_unit, wind_source_unit,
                              sizeof(wind_source_unit));

    EXPECT_EQ(status.weather_daily_forecast_count, 1);
    EXPECT_TRUE(std::strcmp(status.weather_daily_forecast[0].date_text.data(), "2026-01-01") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_daily_forecast[0].temperature_text.data(), "5-15C") ==
               0);
    EXPECT_TRUE(std::strcmp(status.sunrise_text.data(), "07:00") == 0);
    EXPECT_TRUE(std::strcmp(status.sunset_text.data(), "17:00") == 0);
}

HOST_TEST(parse_weather_returns_false_and_clears_forecast_without_hourly_data)
{
    HomeAssistantStatus status = {};
    status.weather_forecast_count = 4;
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    const bool ok = open_meteo::parse_weather("{\"current\":{\"temperature_2m\":10}}", status,
                                              temperature_unit, wind_source_unit,
                                              sizeof(wind_source_unit));

    EXPECT_FALSE(ok);
    EXPECT_EQ(status.weather_forecast_count, 0);
}
