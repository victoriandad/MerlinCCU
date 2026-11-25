// Exceptional UK weather (#202): the threshold table, both sources' parsers
// marking forecast rows, HA's warnings payload, and the warning alert.

#include "weather_extremes.h"

#include <cstdio>
#include <cstring>

#include "alert_controller.h"
#include "console_model.h"
#include "house_weather.h"
#include "open_meteo_parser.h"
#include "screens_shared.h"
#include "test_framework.h"
#include "text_styles.h"

using weather_extremes::Period;
using weather_extremes::Reading;

namespace
{

Reading hour_at(float celsius)
{
    Reading reading = {};
    reading.low_celsius = celsius;
    reading.high_celsius = celsius;
    return reading;
}

// Two hours: the first quiet, the second a gale with a downpour; one day
// with heavy snow.
constexpr const char* kOpenMeteoStorm =
    "{\"current\":{\"temperature_2m\":5.0,\"weather_code\":3,\"wind_speed_10m\":12.0},"
    "\"hourly\":{\"time\":[\"2026-01-01T00:00\",\"2026-01-01T01:00\"],"
    "\"temperature_2m\":[5.0,4.0],\"wind_speed_10m\":[10.0,41.0],"
    "\"wind_direction_10m\":[90,180],\"weather_code\":[3,63],"
    "\"precipitation\":[0.0,6.2],\"snowfall\":[0.0,0.0],\"wind_gusts_10m\":[18.0,62.0],"
    "\"visibility\":[24000,180]},"
    "\"daily\":{\"time\":[\"2026-01-01\"],\"temperature_2m_max\":[6.0],"
    "\"temperature_2m_min\":[-1.0],\"wind_speed_10m_max\":[20.0],"
    "\"wind_direction_10m_dominant\":[270],\"weather_code\":[73],"
    "\"precipitation_sum\":[8.0],\"snowfall_sum\":[7.5],\"wind_gusts_10m_max\":[35.0],"
    "\"sunrise\":[\"2026-01-01T07:00\"],\"sunset\":[\"2026-01-01T17:00\"]}}";

} // namespace

HOST_TEST(weather_extremes_thresholds_hold_at_their_edges)
{
    EXPECT_EQ(weather_extremes::classify(hour_at(0.0F), Period::Hour), weather_extremes::kCold);
    EXPECT_EQ(weather_extremes::classify(hour_at(0.1F), Period::Hour), weather_extremes::kNone);
    EXPECT_EQ(weather_extremes::classify(hour_at(28.0F), Period::Hour), weather_extremes::kHeat);
    EXPECT_EQ(weather_extremes::classify(hour_at(27.9F), Period::Hour), weather_extremes::kNone);

    Reading wind = {};
    wind.wind_mph = 38.9F;
    EXPECT_EQ(weather_extremes::classify(wind, Period::Hour), weather_extremes::kNone);
    wind.wind_mph = 39.0F;
    EXPECT_EQ(weather_extremes::classify(wind, Period::Hour), weather_extremes::kWind);
    wind.wind_mph = 20.0F;
    wind.gust_mph = 50.0F;
    EXPECT_EQ(weather_extremes::classify(wind, Period::Hour), weather_extremes::kWind);

    Reading fog = {};
    fog.visibility_m = 200.0F;
    EXPECT_EQ(weather_extremes::classify(fog, Period::Hour), weather_extremes::kNone);
    fog.visibility_m = 199.0F;
    EXPECT_EQ(weather_extremes::classify(fog, Period::Hour), weather_extremes::kFog);
}

HOST_TEST(weather_extremes_rain_and_snow_use_hour_or_day_limits)
{
    Reading wet = {};
    wet.rain_mm = 4.0F;
    EXPECT_EQ(weather_extremes::classify(wet, Period::Hour), weather_extremes::kRain);
    EXPECT_EQ(weather_extremes::classify(wet, Period::Day), weather_extremes::kNone);
    wet.rain_mm = 20.0F;
    EXPECT_EQ(weather_extremes::classify(wet, Period::Day), weather_extremes::kRain);

    Reading snow = {};
    snow.snow_cm = 1.0F;
    EXPECT_EQ(weather_extremes::classify(snow, Period::Hour), weather_extremes::kSnow);
    EXPECT_EQ(weather_extremes::classify(snow, Period::Day), weather_extremes::kNone);

    // With no amounts, the provider's condition stands in.
    Reading pouring = {};
    pouring.heavy_rain_condition = true;
    EXPECT_EQ(weather_extremes::classify(pouring, Period::Hour), weather_extremes::kRain);
}

HOST_TEST(weather_extremes_combine_on_one_row)
{
    Reading storm = hour_at(-2.0F);
    storm.wind_mph = 45.0F;
    storm.rain_mm = 9.0F;
    EXPECT_EQ(weather_extremes::classify(storm, Period::Hour),
              weather_extremes::kCold | weather_extremes::kWind | weather_extremes::kRain);
}

HOST_TEST(open_meteo_marks_exceptional_hours_and_days)
{
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_unit[8] = {};
    EXPECT_TRUE(open_meteo::parse_weather(kOpenMeteoStorm, status, temperature_unit, wind_unit,
                                          sizeof(wind_unit)));
    EXPECT_EQ(status.weather_forecast[0].hazards, weather_extremes::kNone);
    EXPECT_EQ(status.weather_forecast[1].hazards,
              weather_extremes::kWind | weather_extremes::kRain | weather_extremes::kFog);
    EXPECT_EQ(status.weather_daily_forecast[0].hazards,
              weather_extremes::kCold | weather_extremes::kSnow);
    EXPECT_TRUE(status.weather_metrics.forecast_max_wind_gust_mph_valid);
    EXPECT_TRUE(status.weather_metrics.forecast_max_wind_gust_mph == 62.0F);
}

HOST_TEST(open_meteo_without_the_new_fields_still_parses)
{
    // A response shaped like the request before #202: no precipitation,
    // snowfall, gusts or visibility. The heavy-rain code still marks rain.
    constexpr const char* kOld =
        "{\"current\":{\"temperature_2m\":5.0,\"weather_code\":3,\"wind_speed_10m\":12.0},"
        "\"hourly\":{\"time\":[\"2026-01-01T00:00\"],\"temperature_2m\":[5.0],"
        "\"wind_speed_10m\":[10.0],\"wind_direction_10m\":[90],\"weather_code\":[65]},"
        "\"daily\":{\"time\":[\"2026-01-01\"],\"temperature_2m_max\":[6.0],"
        "\"temperature_2m_min\":[2.0],\"wind_speed_10m_max\":[20.0],"
        "\"wind_direction_10m_dominant\":[270],\"weather_code\":[3]}}";
    HomeAssistantStatus status = {};
    char temperature_unit = '\0';
    char wind_unit[8] = {};
    EXPECT_TRUE(
        open_meteo::parse_weather(kOld, status, temperature_unit, wind_unit, sizeof(wind_unit)));
    EXPECT_EQ(status.weather_forecast[0].hazards, weather_extremes::kRain);
    EXPECT_EQ(status.weather_daily_forecast[0].hazards, weather_extremes::kNone);
}

HOST_TEST(ha_forecast_marks_rain_gusts_and_snow_estimated_from_precipitation)
{
    // HA in mph and inches: 0.2 in is 5 mm, heavy for an hour; a snowy hour's
    // 0.05 in (1.27 mm) is read as 1.27 cm of snow.
    constexpr const char* kHourly =
        "{\"v\":1,\"ts\":1763900000,\"temperature_unit\":\"°C\",\"wind_speed_unit\":\"mph\","
        "\"precipitation_unit\":\"in\",\"forecast\":["
        "{\"datetime\":\"2026-11-23T18:00:00+00:00\",\"condition\":\"rainy\","
        "\"temperature\":7.1,\"precipitation\":0.2,\"wind_speed\":20.0,\"wind_gust_speed\":55.0},"
        "{\"datetime\":\"2026-11-23T19:00:00+00:00\",\"condition\":\"snowy\","
        "\"temperature\":-1.0,\"precipitation\":0.05,\"wind_speed\":5.0},"
        "{\"datetime\":\"2026-11-23T20:00:00+00:00\",\"condition\":\"pouring\","
        "\"temperature\":6.0,\"wind_speed\":5.0}]}";
    HomeAssistantStatus status = {};
    EXPECT_TRUE(house_weather::apply(house_weather::Topic::Hourly, kHourly, status).has_value());
    EXPECT_EQ(status.weather_forecast[0].hazards,
              weather_extremes::kRain | weather_extremes::kWind);
    EXPECT_EQ(status.weather_forecast[1].hazards,
              weather_extremes::kCold | weather_extremes::kSnow);
    EXPECT_EQ(status.weather_forecast[2].hazards, weather_extremes::kRain);
}

HOST_TEST(ha_daily_forecast_uses_the_day_limits)
{
    constexpr const char* kDaily =
        "{\"v\":1,\"ts\":1763900000,\"temperature_unit\":\"°C\",\"wind_speed_unit\":\"mph\","
        "\"forecast\":[{\"datetime\":\"2026-11-24T12:00:00+00:00\",\"condition\":\"rainy\","
        "\"temperature\":29.0,\"templow\":15.0,\"precipitation\":12.0,\"wind_speed\":10.0},"
        "{\"datetime\":\"2026-11-25T12:00:00+00:00\",\"condition\":\"rainy\","
        "\"temperature\":9.0,\"templow\":3.0,\"precipitation\":24.0,\"wind_speed\":10.0}]}";
    HomeAssistantStatus status = {};
    EXPECT_TRUE(house_weather::apply(house_weather::Topic::Daily, kDaily, status).has_value());
    EXPECT_EQ(status.weather_daily_forecast[0].hazards, weather_extremes::kHeat);
    EXPECT_EQ(status.weather_daily_forecast[1].hazards, weather_extremes::kRain);
}

HOST_TEST(house_weather_reads_the_warnings_topic)
{
    EXPECT_TRUE(house_weather::topic_for("merlinccu/house/weather/warnings", "merlinccu") ==
                house_weather::Topic::Warnings);

    constexpr const char* kWarnings =
        "{\"v\":1,\"ts\":1763900000,\"level\":\"amber\",\"summary\":\"AMBER WIND\","
        "\"lines\":[\"Amber wind until 21:00 Tue\",\"Yellow flood alert: River Frome\"]}";
    WeatherWarnings warnings = {};
    warnings.stale = true;
    EXPECT_TRUE(house_weather::apply_warnings(kWarnings, warnings) == 1763900000U);
    EXPECT_TRUE(warnings.level == WarningLevel::Amber);
    EXPECT_TRUE(std::strcmp(warnings.summary.data(), "AMBER WIND") == 0);
    EXPECT_TRUE(std::strcmp(warnings.detail.data(),
                            "Amber wind until 21:00 Tue\nYellow flood alert: River Frome") == 0);
    EXPECT_FALSE(warnings.stale);

    // "none" clears them; an unknown level or schema is refused, keeping them.
    EXPECT_TRUE(house_weather::apply_warnings("{\"v\":1,\"ts\":1763900900,\"level\":\"purple\"}",
                                              warnings) == std::nullopt);
    EXPECT_TRUE(warnings.level == WarningLevel::Amber);
    EXPECT_TRUE(house_weather::apply_warnings("{\"v\":2,\"ts\":1763900900,\"level\":\"none\"}",
                                              warnings) == std::nullopt);
    EXPECT_TRUE(house_weather::apply_warnings(
                    "{\"v\":1,\"ts\":1763900900,\"level\":\"none\",\"lines\":[]}", warnings)
                    .has_value());
    EXPECT_TRUE(warnings.level == WarningLevel::None);
    EXPECT_EQ(warnings.detail[0], '\0');
}

HOST_TEST(official_warning_summaries_fit_an_alert_key)
{
    // HA keeps summaries to 16 characters (weather_warnings.py
    // MAX_SUMMARY_LENGTH); the longest it makes must fit an alert key with
    // its time and count (LBL-6).
    for (const char* summary : {"YELLOW RAIN/WIND", "YELLOW SNOW/ICE", "RED HEAT", "AMBER FLOOD"})
    {
        char label[48] = {};
        std::snprintf(label, sizeof(label), "%s\n[12:34 x99]", summary);
        EXPECT_TRUE(screens::softkey_label_fits(label, text_styles::TextStyle::Softkey, 2));
    }
}

HOST_TEST(official_warning_alert_takes_the_level_as_its_severity)
{
    ConsoleState state = {};
    alert_controller::reset();
    state.weather_warnings.level = WarningLevel::Red;
    std::snprintf(state.weather_warnings.summary.data(), state.weather_warnings.summary.size(),
                  "%s", "RED WIND");
    std::snprintf(state.weather_warnings.detail.data(), state.weather_warnings.detail.size(), "%s",
                  "Red wind until 21:00 Tue");
    state.weather_warnings.stale = true;
    alert_controller::sync(state);

    const ActiveAlert* alert = nullptr;
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (state.active_alerts[i].code == static_cast<uint8_t>(AlertCode::OfficialWeatherWarning))
        {
            alert = &state.active_alerts[i];
        }
    }
    EXPECT_TRUE(alert != nullptr);
    if (alert != nullptr)
    {
        EXPECT_TRUE(alert->severity == AlertSeverity::Alert);
        EXPECT_TRUE(std::strcmp(alert->summary.data(), "RED WIND") == 0);
        EXPECT_TRUE(std::strstr(alert->detail.data(), "Not updated for over 2 hours.") != nullptr);
    }

    state.weather_warnings = {};
    alert_controller::sync(state);
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        EXPECT_TRUE(state.active_alerts[i].code !=
                    static_cast<uint8_t>(AlertCode::OfficialWeatherWarning));
    }
    alert_controller::reset();
}
