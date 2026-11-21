#include "geo_coordinates.h"

#include <cmath>
#include <string>

#include "test_framework.h"

namespace
{

/// @brief Tolerance for comparing parsed degrees: well under 0.01'.
constexpr double kTolerance = 1e-6;

bool parses_to(const char* text, double latitude, double longitude)
{
    const std::optional<geo_coordinates::GeoPoint> point = geo_coordinates::parse(text);
    return point && std::fabs(point->latitude - latitude) < kTolerance &&
           std::fabs(point->longitude - longitude) < kTolerance;
}

bool rejects(const char* text)
{
    return !geo_coordinates::parse(text).has_value();
}

std::string degrees_minutes(double latitude, double longitude, const char* mark)
{
    char out[48] = {};
    EXPECT_TRUE(
        geo_coordinates::format_degrees_minutes({latitude, longitude}, mark, out, sizeof(out)));
    return out;
}

constexpr double kLat = 50.23;
constexpr double kLon = -2.39;

} // namespace

HOST_TEST(geo_parse_reads_signed_decimal_latitude_first)
{
    EXPECT_TRUE(parses_to("50.23,-2.39", kLat, kLon));
    EXPECT_TRUE(parses_to(" 50.23 , -2.39 ", kLat, kLon));
    EXPECT_TRUE(parses_to("50.23 -2.39", kLat, kLon));
    EXPECT_TRUE(parses_to("-33.86 151.21", -33.86, 151.21));
}

HOST_TEST(geo_parse_reads_hemisphere_letters_before_or_after_in_either_order)
{
    EXPECT_TRUE(parses_to("N50.23 W2.39", kLat, kLon));
    EXPECT_TRUE(parses_to("50.23N 2.39W", kLat, kLon));
    EXPECT_TRUE(parses_to("W2.39 N50.23", kLat, kLon));
    EXPECT_TRUE(parses_to("n50.23, w2.39", kLat, kLon));
    EXPECT_TRUE(parses_to("S33.86 E151.21", -33.86, 151.21));
}

HOST_TEST(geo_parse_reads_degrees_minutes_and_seconds_with_or_without_marks)
{
    constexpr double kDmLat = 50.0 + (13.8 / 60.0);
    constexpr double kDmLon = -(2.0 + (23.4 / 60.0));
    EXPECT_TRUE(parses_to("N50\xC2\xB0" "13.80' W002\xC2\xB0" "23.40'", kDmLat, kDmLon));
    EXPECT_TRUE(parses_to("N50 13.8 W2 23.4", kDmLat, kDmLon));
    EXPECT_TRUE(parses_to("50:13.8N 2:23.4W", kDmLat, kDmLon));
    EXPECT_TRUE(parses_to("N50 13 48 W2 23 24", kDmLat, kDmLon));
    EXPECT_TRUE(parses_to("N50\xC2\xB0" "13'48\" W2\xC2\xB0" "23'24\"", kDmLat, kDmLon));
    EXPECT_TRUE(parses_to("50 13.8, -2 23.4", kDmLat, kDmLon));
}

HOST_TEST(geo_parse_rejects_ambiguous_or_impossible_positions)
{
    EXPECT_TRUE(rejects(""));
    EXPECT_TRUE(rejects("50.23"));
    EXPECT_TRUE(rejects("N50.23 N2.39"));    // Two latitudes.
    EXPECT_TRUE(rejects("N50.23 -2.39W"));   // A letter and a sign.
    EXPECT_TRUE(rejects("N50 61 W2 0"));     // Minutes past 59.
    EXPECT_TRUE(rejects("N50.5 13 W2 23"));  // Fraction before minutes.
    EXPECT_TRUE(rejects("91, 0"));
    EXPECT_TRUE(rejects("0, 181"));
    EXPECT_TRUE(rejects("50.23 -2.39 7"));   // Odd number of parts.
    EXPECT_TRUE(rejects("London"));
    EXPECT_TRUE(rejects("50.2.3, 1"));
}

HOST_TEST(geo_format_writes_degrees_and_minutes_with_the_given_degree_mark)
{
    EXPECT_TRUE(degrees_minutes(kLat, kLon, "\xC2\xB0") ==
                "N50\xC2\xB0" "13.80' W002\xC2\xB0" "23.40'");
    EXPECT_TRUE(degrees_minutes(-33.86, 151.21, " ") == "S33 51.60' E151 12.60'");
    // 0.99999 degrees rounds to a whole degree, not 60.00 minutes.
    EXPECT_TRUE(degrees_minutes(0.99999, 0.0, " ") == "N01 00.00' E000 00.00'");
}

HOST_TEST(geo_formats_round_trip_through_parse)
{
    char text[48] = {};
    EXPECT_TRUE(geo_coordinates::format_decimal({kLat, kLon}, text, sizeof(text)));
    EXPECT_TRUE(std::string(text) == "50.230000,-2.390000");
    EXPECT_TRUE(parses_to(text, kLat, kLon));

    EXPECT_TRUE(
        geo_coordinates::format_degrees_minutes({kLat, kLon}, "\xC2\xB0", text, sizeof(text)));
    EXPECT_TRUE(parses_to(text, kLat, kLon));
}
