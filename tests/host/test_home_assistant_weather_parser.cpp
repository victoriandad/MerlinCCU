#include "home_assistant_weather_parser.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(update_weather_source_hint_from_json_prefers_attribution)
{
    HomeAssistantStatus status = {};
    home_assistant_weather::update_weather_source_hint_from_json(
        "{\"attribution\":\"Weather forecast from Open-Meteo.com\",\"friendly_name\":\"Home "
        "Weather\"}",
        status);
    EXPECT_TRUE(std::strcmp(status.weather_source_hint.data(), "Open-Meteo.com") == 0);
}

HOST_TEST(update_weather_source_hint_from_json_falls_back_to_friendly_name)
{
    HomeAssistantStatus status = {};
    home_assistant_weather::update_weather_source_hint_from_json(
        "{\"friendly_name\":\"Home Weather\"}", status);
    EXPECT_TRUE(std::strcmp(status.weather_source_hint.data(), "Home Weather") == 0);
}

HOST_TEST(update_sun_times_from_json_extracts_rising_and_setting_times)
{
    HomeAssistantStatus status = {};
    home_assistant_weather::update_sun_times_from_json(
        "{\"next_rising\":\"2026-01-01T06:45:00+00:00\",\"next_setting\":\"2026-01-01T16:"
        "30:00+00:00\"}",
        status);
    EXPECT_TRUE(std::strcmp(status.sunrise_text.data(), "06:45") == 0);
    EXPECT_TRUE(std::strcmp(status.sunset_text.data(), "16:30") == 0);
}

HOST_TEST(parse_current_weather_entity_populates_condition_temperature_and_wind)
{
    const char* json =
        "{\"state\":\"cloudy\",\"attributes\":{\"temperature\":18,\"temperature_unit\":\"°C\","
        "\"wind_speed\":25,\"wind_speed_unit\":\"km/h\"}}";

    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    const bool have_state = home_assistant_weather::parse_current_weather_entity(
        json, status, temperature_unit, wind_source_unit, sizeof(wind_source_unit));

    EXPECT_TRUE(have_state);
    EXPECT_TRUE(std::strcmp(status.weather_condition.data(), "CLOUDY") == 0);
    EXPECT_EQ(temperature_unit, 'C');
    EXPECT_TRUE(std::strcmp(status.weather_temperature.data(), "18 C") == 0);
    EXPECT_TRUE(status.weather_metrics.current_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.current_wind_speed_mph_valid);
    EXPECT_TRUE(std::strcmp(status.weather_wind_unit.data(), "mph") == 0);
}

HOST_TEST(parse_current_weather_entity_reports_unknown_state_as_question_mark)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    const bool have_state = home_assistant_weather::parse_current_weather_entity(
        "{\"attributes\":{}}", status, temperature_unit, wind_source_unit,
        sizeof(wind_source_unit));

    EXPECT_FALSE(have_state);
    EXPECT_TRUE(std::strcmp(status.weather_condition.data(), "?") == 0);
}

HOST_TEST(parse_current_weather_entity_records_a_warning_for_severe_conditions)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};
    home_assistant_weather::parse_current_weather_entity("{\"state\":\"lightning\"}", status,
                                                          temperature_unit, wind_source_unit,
                                                          sizeof(wind_source_unit));

    EXPECT_TRUE(status.weather_alert_status.provider_warning_active);
}

HOST_TEST(parse_current_weather_entity_resets_previous_metrics_each_call)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_source_unit[8] = {};

    home_assistant_weather::parse_current_weather_entity(
        "{\"state\":\"sunny\",\"attributes\":{\"temperature\":30,\"temperature_unit\":\"°C\"}}",
        status, temperature_unit, wind_source_unit, sizeof(wind_source_unit));
    EXPECT_TRUE(status.weather_metrics.current_temperature_celsius_valid);

    // A second call without a temperature attribute must clear the stale
    // reading from the first call rather than leaving it in place.
    home_assistant_weather::parse_current_weather_entity("{\"state\":\"cloudy\"}", status,
                                                          temperature_unit, wind_source_unit,
                                                          sizeof(wind_source_unit));
    EXPECT_FALSE(status.weather_metrics.current_temperature_celsius_valid);
}
