#include "weather_json_scan.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(extract_json_string_value_finds_a_quoted_field)
{
    char out[32] = {};
    EXPECT_TRUE(weather_json::extract_json_string_value("{\"state\":\"cloudy\"}", "\"state\":\"",
                                                         out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "cloudy") == 0);
}

HOST_TEST(extract_json_string_value_rejects_an_unterminated_string)
{
    char out[32] = {};
    EXPECT_FALSE(weather_json::extract_json_string_value("{\"state\":\"cloudy", "\"state\":\"", out,
                                                          sizeof(out)));
}

HOST_TEST(extract_json_string_value_returns_false_when_key_absent)
{
    char out[32] = {};
    EXPECT_FALSE(
        weather_json::extract_json_string_value("{\"other\":1}", "\"state\":\"", out, sizeof(out)));
}

HOST_TEST(extract_json_scalar_value_finds_a_numeric_field)
{
    char out[16] = {};
    EXPECT_TRUE(weather_json::extract_json_scalar_value("{\"temperature\":21.5}",
                                                         "\"temperature\":", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "21.5") == 0);
}

HOST_TEST(extract_json_scalar_value_rejects_a_quoted_value)
{
    char out[16] = {};
    EXPECT_FALSE(weather_json::extract_json_scalar_value("{\"temperature\":\"21.5\"}",
                                                          "\"temperature\":", out, sizeof(out)));
}

HOST_TEST(find_matching_json_object_end_skips_braces_inside_strings)
{
    const char* json = "{\"note\":\"a {brace} inside\",\"x\":1}";
    const char* end = weather_json::find_matching_json_object_end(json);
    EXPECT_TRUE(end != nullptr);
    EXPECT_TRUE(*end == '}');
    EXPECT_TRUE(end == json + std::strlen(json) - 1);
}

HOST_TEST(find_matching_json_object_end_returns_null_for_unterminated_object)
{
    EXPECT_TRUE(weather_json::find_matching_json_object_end("{\"a\":1") == nullptr);
}

HOST_TEST(find_json_array_start_locates_the_first_element)
{
    const char* array = weather_json::find_json_array_start("{\"forecast\":[1,2,3]}", "\"forecast\":");
    EXPECT_TRUE(array != nullptr);
    EXPECT_TRUE(*array == '1');
}

HOST_TEST(json_array_string_at_returns_the_requested_index)
{
    char out[16] = {};
    const char* array = "\"a\",\"bb\",\"ccc\"]";
    EXPECT_TRUE(weather_json::json_array_string_at(array, 1, out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "bb") == 0);
}

HOST_TEST(json_array_string_at_returns_false_past_the_end)
{
    char out[16] = {};
    const char* array = "\"a\",\"b\"]";
    EXPECT_FALSE(weather_json::json_array_string_at(array, 5, out, sizeof(out)));
}

HOST_TEST(json_array_number_at_returns_the_requested_index)
{
    char out[16] = {};
    const char* array = "1.5,2.5,3.5]";
    EXPECT_TRUE(weather_json::json_array_number_at(array, 2, out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "3.5") == 0);
}

HOST_TEST(open_meteo_daily_scalar_falls_back_to_the_legacy_key)
{
    char out[16] = {};
    const char* daily = "\"daily\":{\"time\":[\"2026-01-01\"],\"windspeed_10m_max\":[12.0]}";
    EXPECT_TRUE(weather_json::open_meteo_daily_scalar(
        daily, "\"wind_speed_10m_max\":", "\"windspeed_10m_max\":", 0, out, sizeof(out), false));
    EXPECT_TRUE(std::strcmp(out, "12.0") == 0);
}

HOST_TEST(open_meteo_daily_scalar_prefers_the_primary_key)
{
    char out[16] = {};
    const char* daily =
        "\"daily\":{\"wind_speed_10m_max\":[9.0],\"windspeed_10m_max\":[12.0]}";
    EXPECT_TRUE(weather_json::open_meteo_daily_scalar(
        daily, "\"wind_speed_10m_max\":", "\"windspeed_10m_max\":", 0, out, sizeof(out), false));
    EXPECT_TRUE(std::strcmp(out, "9.0") == 0);
}
