#include "bounded_json.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(extract_bounded_string_reads_a_quoted_field_within_range)
{
    const char* json = "{\"symbol\":\"BA.L\",\"name\":\"BAE SYSTEMS\"}";
    const char* end = json + std::strlen(json);
    char out[16] = {};
    EXPECT_TRUE(bounded_json::extract_bounded_string(json, end, "\"symbol\"", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "BA.L") == 0);
}

HOST_TEST(extract_bounded_string_does_not_read_past_the_object_boundary)
{
    // Two sibling objects share a key name; bounding to the first object's
    // range must not let the search bleed into the second.
    const char* json = "{\"a\":{\"symbol\":\"FIRST\"}},{\"symbol\":\"SECOND\"}";
    const char* first_end = std::strchr(json, '}') + 1;
    char out[16] = {};
    EXPECT_TRUE(
        bounded_json::extract_bounded_string(json, first_end, "\"symbol\"", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "FIRST") == 0);
}

HOST_TEST(extract_bounded_number_reads_a_numeric_field)
{
    const char* json = "{\"dst\":12.4,\"dir\":270}";
    const char* end = json + std::strlen(json);
    double value = 0.0;
    EXPECT_TRUE(bounded_json::extract_bounded_number(json, end, "\"dst\"", &value));
    EXPECT_TRUE(value > 12.39 && value < 12.41);
}

HOST_TEST(extract_bounded_number_rejects_a_quoted_value)
{
    const char* json = "{\"price\":\"1372.0\"}";
    const char* end = json + std::strlen(json);
    double value = 0.0;
    EXPECT_FALSE(bounded_json::extract_bounded_number(json, end, "\"price\"", &value));
}

HOST_TEST(extract_bounded_number_returns_false_when_key_is_missing)
{
    const char* json = "{\"other\":1}";
    const char* end = json + std::strlen(json);
    double value = 0.0;
    EXPECT_FALSE(bounded_json::extract_bounded_number(json, end, "\"dst\"", &value));
}

HOST_TEST(bounded_value_is_string_distinguishes_quoted_from_numeric)
{
    const char* json = "{\"alt_baro\":\"ground\"}";
    const char* end = json + std::strlen(json);
    EXPECT_TRUE(bounded_json::bounded_value_is_string(json, end, "\"alt_baro\""));

    const char* numeric_json = "{\"alt_baro\":5000}";
    const char* numeric_end = numeric_json + std::strlen(numeric_json);
    EXPECT_FALSE(bounded_json::bounded_value_is_string(numeric_json, numeric_end, "\"alt_baro\""));
}

HOST_TEST(find_object_end_is_string_aware)
{
    const char* json = "{\"note\":\"a } inside a string\",\"x\":1}";
    const char* end = bounded_json::find_object_end(json, json + std::strlen(json));
    EXPECT_TRUE(end != nullptr);
    EXPECT_TRUE(*end == '}');
    EXPECT_TRUE(end == json + std::strlen(json) - 1);
}

HOST_TEST(find_object_end_returns_null_for_a_truncated_object)
{
    const char* json = "{\"symbol\":\"BA.L\"";
    EXPECT_TRUE(bounded_json::find_object_end(json, json + std::strlen(json)) == nullptr);
}

HOST_TEST(extract_bounded_string_accepts_text_with_no_closing_quote_in_range)
{
    // Unlike weather_json::extract_json_string_value() (which explicitly
    // requires a closing quote and treats a truncated value as "missing"),
    // this bounded variant just copies up to `end`/out_size/quote, whichever
    // comes first -- a pre-existing difference between the two JSON helpers
    // inherited as-is from the original share/air-traffic code, not
    // something this extraction changed.
    const char* json = "{\"symbol\":\"BA.L";
    const char* end = json + std::strlen(json);
    char out[16] = {};
    EXPECT_TRUE(bounded_json::extract_bounded_string(json, end, "\"symbol\"", out, sizeof(out)));
    EXPECT_TRUE(std::strcmp(out, "BA.L") == 0);
}
