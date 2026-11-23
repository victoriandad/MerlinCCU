#include "house_weather.h"

#include <cstring>

#include "test_framework.h"

using house_weather::Topic;

namespace
{

// Shaped like ha/custom_components/merlinccu/house_weather.py's output (#153).
constexpr const char* kNow =
    "{\"v\":1,\"ts\":1763900000,\"attribution\":\"Weather forecast from met.no, delivered by "
    "the Norwegian Meteorological Institute.\",\"state\":\"rainy\",\"temperature\":7.2,"
    "\"temperature_unit\":\"°C\",\"wind_speed\":14.4,\"wind_speed_unit\":\"km/h\","
    "\"wind_bearing\":225,\"next_rising\":\"2026-11-24T07:41:00+00:00\","
    "\"next_setting\":\"2026-11-24T16:01:00+00:00\"}";

constexpr const char* kHourly =
    "{\"v\":1,\"ts\":1763900000,\"temperature_unit\":\"°C\",\"wind_speed_unit\":\"km/h\","
    "\"forecast\":[{\"datetime\":\"2026-11-23T18:00:00+00:00\",\"condition\":\"rainy\","
    "\"temperature\":7.1,\"wind_speed\":14.4,\"wind_bearing\":225},"
    "{\"datetime\":\"2026-11-23T19:00:00+00:00\",\"condition\":\"cloudy\","
    "\"temperature\":6.8,\"wind_speed\":12.0,\"wind_bearing\":230}]}";

constexpr const char* kDaily =
    "{\"v\":1,\"ts\":1763900000,\"temperature_unit\":\"°C\",\"wind_speed_unit\":\"km/h\","
    "\"forecast\":[{\"datetime\":\"2026-11-24T12:00:00+00:00\",\"condition\":\"sunny\","
    "\"temperature\":9.0,\"templow\":3.5,\"wind_speed\":10.0,\"wind_bearing\":270}]}";

} // namespace

HOST_TEST(house_weather_topics_match_only_the_three_weather_topics)
{
    EXPECT_TRUE(house_weather::topic_for("merlinccu/house/weather/now", "merlinccu") == Topic::Now);
    EXPECT_TRUE(house_weather::topic_for("merlinccu/house/weather/hourly", "merlinccu") ==
                Topic::Hourly);
    EXPECT_TRUE(house_weather::topic_for("merlinccu/house/weather/daily", "merlinccu") ==
                Topic::Daily);
    EXPECT_FALSE(house_weather::topic_for("merlinccu/house/weather/weekly", "merlinccu"));
    EXPECT_FALSE(house_weather::topic_for("other/house/weather/now", "merlinccu"));
    EXPECT_FALSE(house_weather::topic_for("merlinccu/28cdc11a896c/page/set", "merlinccu"));
    EXPECT_FALSE(house_weather::topic_for("merlinccu/house/weather/now", ""));
}

HOST_TEST(house_weather_now_fills_conditions_provider_and_sun_times)
{
    HomeAssistantStatus status = {};
    const auto timestamp = house_weather::apply(Topic::Now, kNow, status);
    EXPECT_TRUE(timestamp == 1763900000U);
    EXPECT_TRUE(status.weather_condition[0] != '\0');
    EXPECT_TRUE(std::strncmp(status.weather_temperature.data(), "7.2", 3) == 0);
    EXPECT_TRUE(status.weather_metrics.current_temperature_celsius_valid);
    EXPECT_TRUE(status.weather_metrics.current_wind_speed_mph_valid);
    EXPECT_TRUE(status.weather_source_hint[0] != '\0');
    EXPECT_TRUE(std::strcmp(status.sunrise_text.data(), "07:41") == 0);
    EXPECT_TRUE(std::strcmp(status.sunset_text.data(), "16:01") == 0);
}

HOST_TEST(house_weather_forecasts_carry_their_own_units)
{
    // No "now" first: each forecast payload names its units, so retained
    // topics can arrive in any order.
    HomeAssistantStatus status = {};
    EXPECT_TRUE(house_weather::apply(Topic::Hourly, kHourly, status).has_value());
    EXPECT_EQ(status.weather_forecast_count, 2U);
    EXPECT_TRUE(std::strstr(status.weather_forecast[0].temperature_text.data(), "C") != nullptr);
    EXPECT_TRUE(status.weather_metrics.forecast_max_wind_speed_mph_valid);

    EXPECT_TRUE(house_weather::apply(Topic::Daily, kDaily, status).has_value());
    EXPECT_EQ(status.weather_daily_forecast_count, 1U);
}

HOST_TEST(house_weather_refuses_other_versions_and_missing_fields)
{
    HomeAssistantStatus status = {};
    EXPECT_TRUE(house_weather::apply(Topic::Hourly, kHourly, status).has_value());

    // Each refusal leaves the forecast already shown in place.
    EXPECT_FALSE(house_weather::apply(Topic::Hourly,
                                      "{\"v\":2,\"ts\":1763900000,\"forecast\":[]}", status));
    EXPECT_FALSE(house_weather::apply(Topic::Hourly, "{\"v\":1,\"forecast\":[]}", status));
    EXPECT_FALSE(house_weather::apply(Topic::Hourly, "{\"v\":1,\"ts\":1763900000}", status));
    EXPECT_FALSE(house_weather::apply(Topic::Now, "{\"v\":1,\"ts\":1763900000}", status));
    EXPECT_FALSE(house_weather::apply(Topic::Now, "not json", status));
    EXPECT_EQ(status.weather_forecast_count, 2U);
}
