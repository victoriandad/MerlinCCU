#include "weather_forecast_parser.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(include_forecast_temperature_celsius_tracks_min_and_max)
{
    HomeAssistantStatus status = {};
    weather_forecast::include_forecast_temperature_celsius(status, 5.0F);
    weather_forecast::include_forecast_temperature_celsius(status, 12.0F);
    weather_forecast::include_forecast_temperature_celsius(status, 2.0F);

    EXPECT_TRUE(status.weather_metrics.forecast_min_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.forecast_max_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.forecast_min_temperature_celsius == 2.0F);
    EXPECT_TRUE(status.weather_metrics.forecast_max_temperature_celsius == 12.0F);
}

HOST_TEST(include_forecast_wind_speed_mph_tracks_the_maximum)
{
    HomeAssistantStatus status = {};
    weather_forecast::include_forecast_wind_speed_mph(status, 8.0F);
    weather_forecast::include_forecast_wind_speed_mph(status, 20.0F);
    weather_forecast::include_forecast_wind_speed_mph(status, 3.0F);

    EXPECT_TRUE(status.weather_metrics.forecast_max_wind_speed_mph_valid);
    EXPECT_TRUE(status.weather_metrics.forecast_max_wind_speed_mph == 20.0F);
}

HOST_TEST(record_weather_condition_warning_sets_warning_for_severe_conditions)
{
    HomeAssistantStatus status = {};
    weather_forecast::record_weather_condition_warning(status, "lightning", " now");
    EXPECT_TRUE(status.weather_alert_status.provider_warning_active);
    EXPECT_EQ(static_cast<int>(status.weather_alert_status.provider_warning_severity),
              static_cast<int>(AlertSeverity::Warning));
}

HOST_TEST(record_weather_condition_warning_ignores_benign_conditions)
{
    HomeAssistantStatus status = {};
    weather_forecast::record_weather_condition_warning(status, "cloudy", " now");
    EXPECT_FALSE(status.weather_alert_status.provider_warning_active);
}

HOST_TEST(clear_weather_forecast_resets_rows_count_and_metrics)
{
    HomeAssistantStatus status = {};
    status.weather_forecast_count = 3;
    weather_forecast::include_forecast_temperature_celsius(status, 9.0F);
    weather_forecast::clear_weather_forecast(status);

    EXPECT_EQ(status.weather_forecast_count, 0);
    EXPECT_FALSE(status.weather_metrics.forecast_max_temperature_celsius_valid);
}

HOST_TEST(parse_hourly_forecast_response_parses_rows_and_updates_metrics)
{
    const char* json =
        "{\"forecast\":["
        "{\"datetime\":\"2026-01-01T10:00:00+00:00\",\"temperature\":5,\"wind_speed\":10,"
        "\"wind_bearing\":180,\"condition\":\"cloudy\"},"
        "{\"datetime\":\"2026-01-01T11:00:00+00:00\",\"temperature\":15,\"wind_speed\":20,"
        "\"wind_bearing\":90,\"condition\":\"sunny\"}"
        "]}";

    HomeAssistantStatus status = {};
    EXPECT_TRUE(weather_forecast::parse_hourly_forecast_response(json, status, 'C', "km/h"));
    EXPECT_EQ(status.weather_forecast_count, 2);
    EXPECT_TRUE(std::strcmp(status.weather_forecast[0].condition_text.data(), "CLOUDY") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_forecast[1].condition_text.data(), "SUNNY") == 0);
    EXPECT_TRUE(status.weather_metrics.forecast_min_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.forecast_min_temperature_celsius == 5.0F);
    EXPECT_TRUE(status.weather_metrics.forecast_max_temperature_celsius == 15.0F);
}

HOST_TEST(parse_hourly_forecast_response_returns_false_without_a_forecast_key)
{
    HomeAssistantStatus status = {};
    EXPECT_FALSE(weather_forecast::parse_hourly_forecast_response("{\"other\":1}", status, 'C',
                                                                   "mph"));
}

HOST_TEST(parse_daily_forecast_response_parses_high_low_rows)
{
    const char* json =
        "{\"forecast\":["
        "{\"datetime\":\"2026-01-01\",\"temperature\":12,\"templow\":3,\"wind_speed\":15,"
        "\"wind_bearing\":270,\"condition\":\"rainy\"}"
        "]}";

    HomeAssistantStatus status = {};
    EXPECT_TRUE(weather_forecast::parse_daily_forecast_response(json, status, 'C', "km/h"));
    EXPECT_EQ(status.weather_daily_forecast_count, 1);
    EXPECT_TRUE(std::strcmp(status.weather_daily_forecast[0].date_text.data(), "2026-01-01") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_daily_forecast[0].temperature_text.data(), "3-12C") == 0);
    EXPECT_TRUE(std::strcmp(status.weather_daily_forecast[0].condition_text.data(), "RAIN") == 0);
}
