#include "geo_coordinates.h"

#include <array>
#include <cmath>
#include <string>

#include "test_framework.h"

namespace
{

bool nearly_equal(double a, double b)
{
    return std::fabs(a - b) < 0.0001;
}

} // namespace

HOST_TEST(parse_coordinates_text_reads_a_signed_decimal_pair)
{
    double lat = 0.0;
    double lon = 0.0;
    EXPECT_TRUE(geo_coordinates::parse_coordinates_text("51.5074,-0.1278", &lat, &lon));
    EXPECT_TRUE(nearly_equal(lat, 51.5074));
    EXPECT_TRUE(nearly_equal(lon, -0.1278));
}

HOST_TEST(parse_coordinates_text_tolerates_arbitrary_separators)
{
    double lat = 0.0;
    double lon = 0.0;
    EXPECT_TRUE(geo_coordinates::parse_coordinates_text("50.23 -2.39", &lat, &lon));
    EXPECT_TRUE(nearly_equal(lat, 50.23));
    EXPECT_TRUE(nearly_equal(lon, -2.39));
}

HOST_TEST(parse_coordinates_text_rejects_out_of_range_latitude)
{
    double lat = 0.0;
    double lon = 0.0;
    EXPECT_FALSE(geo_coordinates::parse_coordinates_text("91.0,0.0", &lat, &lon));
}

HOST_TEST(parse_coordinates_text_rejects_out_of_range_longitude)
{
    double lat = 0.0;
    double lon = 0.0;
    EXPECT_FALSE(geo_coordinates::parse_coordinates_text("0.0,-181.0", &lat, &lon));
}

HOST_TEST(parse_coordinates_text_rejects_garbage_and_empty_text)
{
    double lat = 0.0;
    double lon = 0.0;
    EXPECT_FALSE(geo_coordinates::parse_coordinates_text("not a coordinate", &lat, &lon));
    EXPECT_FALSE(geo_coordinates::parse_coordinates_text("", &lat, &lon));
    EXPECT_FALSE(geo_coordinates::parse_coordinates_text(nullptr, &lat, &lon));
}

HOST_TEST(format_coordinates_text_writes_four_decimal_places)
{
    std::array<char, 32> out = {};
    EXPECT_TRUE(geo_coordinates::format_coordinates_text(51.5074, -0.1278, out.data(), out.size()));
    EXPECT_TRUE(std::string(out.data()) == "51.5074,-0.1278");
}

HOST_TEST(format_coordinates_text_rejects_a_buffer_that_is_too_small)
{
    std::array<char, 4> out = {};
    EXPECT_FALSE(geo_coordinates::format_coordinates_text(51.5074, -0.1278, out.data(), out.size()));
}

HOST_TEST(combine_hemisphere_applies_the_sign_for_each_axis)
{
    double signed_value = 0.0;
    EXPECT_TRUE(geo_coordinates::combine_hemisphere(50.23, 'N', /*is_latitude=*/true, &signed_value));
    EXPECT_TRUE(nearly_equal(signed_value, 50.23));

    EXPECT_TRUE(geo_coordinates::combine_hemisphere(50.23, 's', /*is_latitude=*/true, &signed_value));
    EXPECT_TRUE(nearly_equal(signed_value, -50.23));

    EXPECT_TRUE(geo_coordinates::combine_hemisphere(2.39, 'e', /*is_latitude=*/false, &signed_value));
    EXPECT_TRUE(nearly_equal(signed_value, 2.39));

    EXPECT_TRUE(geo_coordinates::combine_hemisphere(2.39, 'W', /*is_latitude=*/false, &signed_value));
    EXPECT_TRUE(nearly_equal(signed_value, -2.39));
}

HOST_TEST(combine_hemisphere_rejects_an_out_of_range_magnitude_or_wrong_axis_letter)
{
    double signed_value = 0.0;
    EXPECT_FALSE(geo_coordinates::combine_hemisphere(91.0, 'N', /*is_latitude=*/true, &signed_value));
    EXPECT_FALSE(geo_coordinates::combine_hemisphere(-1.0, 'N', /*is_latitude=*/true, &signed_value));
    // 'E'/'W' are not valid hemisphere letters for a latitude.
    EXPECT_FALSE(geo_coordinates::combine_hemisphere(50.0, 'E', /*is_latitude=*/true, &signed_value));
}

HOST_TEST(split_hemisphere_is_the_inverse_of_combine_hemisphere)
{
    double magnitude = 0.0;
    char hemisphere = '\0';
    EXPECT_TRUE(geo_coordinates::split_hemisphere(-50.23, /*is_latitude=*/true, &magnitude, &hemisphere));
    EXPECT_TRUE(nearly_equal(magnitude, 50.23));
    EXPECT_EQ(hemisphere, 'S');

    EXPECT_TRUE(geo_coordinates::split_hemisphere(2.39, /*is_latitude=*/false, &magnitude, &hemisphere));
    EXPECT_TRUE(nearly_equal(magnitude, 2.39));
    EXPECT_EQ(hemisphere, 'E');
}

HOST_TEST(format_parse_and_split_round_trip_at_four_decimal_places)
{
    std::array<char, 32> text = {};
    EXPECT_TRUE(geo_coordinates::format_coordinates_text(50.2300, -2.3900, text.data(), text.size()));

    double lat = 0.0;
    double lon = 0.0;
    EXPECT_TRUE(geo_coordinates::parse_coordinates_text(text.data(), &lat, &lon));

    double lat_magnitude = 0.0;
    char lat_hemisphere = '\0';
    EXPECT_TRUE(geo_coordinates::split_hemisphere(lat, /*is_latitude=*/true, &lat_magnitude, &lat_hemisphere));
    EXPECT_TRUE(nearly_equal(lat_magnitude, 50.23));
    EXPECT_EQ(lat_hemisphere, 'N');

    double lon_magnitude = 0.0;
    char lon_hemisphere = '\0';
    EXPECT_TRUE(geo_coordinates::split_hemisphere(lon, /*is_latitude=*/false, &lon_magnitude, &lon_hemisphere));
    EXPECT_TRUE(nearly_equal(lon_magnitude, 2.39));
    EXPECT_EQ(lon_hemisphere, 'W');
}
