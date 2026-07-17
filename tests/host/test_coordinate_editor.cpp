#include "coordinate_editor.h"

#include <cstring>

#include "test_framework.h"

HOST_TEST(start_preloads_both_axes_from_stored_text)
{
    coordinate_editor::State state;
    EXPECT_TRUE(coordinate_editor::start(state, coordinate_editor::Field::AirTraffic,
                                        "51.5074,-0.1278"));
    EXPECT_TRUE(state.editing);
    EXPECT_TRUE(state.axis == coordinate_editor::Axis::Lat);
    EXPECT_TRUE(std::strcmp(state.lat_buffer.data(), "51.5074") == 0);
    EXPECT_EQ(state.lat_hemisphere, 'N');
    EXPECT_TRUE(std::strcmp(state.lon_buffer.data(), "0.1278") == 0);
    EXPECT_EQ(state.lon_hemisphere, 'W');
}

HOST_TEST(start_falls_back_to_zero_when_stored_text_is_unparseable)
{
    coordinate_editor::State state;
    EXPECT_TRUE(coordinate_editor::start(state, coordinate_editor::Field::Weather, "garbage"));
    EXPECT_TRUE(std::strcmp(state.lat_buffer.data(), "0.0000") == 0);
    EXPECT_EQ(state.lat_hemisphere, 'N');
    EXPECT_TRUE(std::strcmp(state.lon_buffer.data(), "0.0000") == 0);
    EXPECT_EQ(state.lon_hemisphere, 'E');
}

HOST_TEST(start_rejects_a_second_edit_while_one_is_in_progress)
{
    coordinate_editor::State state;
    EXPECT_TRUE(coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, ""));
    EXPECT_FALSE(coordinate_editor::start(state, coordinate_editor::Field::Weather, ""));
}

HOST_TEST(stop_ends_an_edit_and_reports_whether_one_was_in_progress)
{
    coordinate_editor::State state;
    EXPECT_FALSE(coordinate_editor::stop(state));
    EXPECT_TRUE(coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, ""));
    EXPECT_TRUE(coordinate_editor::stop(state));
    EXPECT_FALSE(state.editing);
}

HOST_TEST(apply_digit_appends_calculator_style_to_the_active_axis)
{
    // Appends onto whatever is already in the buffer -- unlike the
    // screensaver timeout scratchpad, there is no replace-on-first-digit
    // behaviour, so starting from the cleared "0" and typing "5" then "0"
    // yields the literal text "050" (still parses to the same value as "50").
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    (void)coordinate_editor::clear_buffer(state);
    EXPECT_TRUE(coordinate_editor::apply_digit(state, 5));
    EXPECT_TRUE(coordinate_editor::apply_digit(state, 0));
    EXPECT_TRUE(std::strcmp(state.lat_buffer.data(), "050") == 0);
}

HOST_TEST(apply_digit_is_a_no_op_once_the_buffer_is_full)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    for (int i = 0; i < 20; ++i)
    {
        (void)coordinate_editor::apply_digit(state, 9);
    }
    // Buffer is 10 bytes: at most 9 usable characters plus the terminator,
    // regardless of how many digits are pressed.
    EXPECT_TRUE(std::strlen(state.lat_buffer.data()) <= 9U);
}

HOST_TEST(apply_dot_is_rejected_when_the_buffer_already_has_one)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    (void)coordinate_editor::clear_buffer(state);
    EXPECT_TRUE(coordinate_editor::apply_dot(state));
    EXPECT_FALSE(coordinate_editor::apply_dot(state));
    EXPECT_TRUE(std::strcmp(state.lat_buffer.data(), "0.") == 0);
}

HOST_TEST(clear_buffer_resets_to_zero_and_reports_whether_it_changed_anything)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "51.5074,-0.1278");
    EXPECT_TRUE(coordinate_editor::clear_buffer(state));
    EXPECT_TRUE(std::strcmp(state.lat_buffer.data(), "0") == 0);
    EXPECT_FALSE(coordinate_editor::clear_buffer(state)); // already "0" -- no change
}

HOST_TEST(toggle_hemisphere_flips_only_the_active_axis)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "1,1");
    EXPECT_TRUE(coordinate_editor::toggle_hemisphere(state));
    EXPECT_EQ(state.lat_hemisphere, 'S');
    EXPECT_EQ(state.lon_hemisphere, 'E'); // unaffected while editing the lat axis

    EXPECT_TRUE(coordinate_editor::advance_axis(state));
    EXPECT_TRUE(coordinate_editor::toggle_hemisphere(state));
    EXPECT_EQ(state.lon_hemisphere, 'W');
    EXPECT_EQ(state.lat_hemisphere, 'S'); // unaffected while editing the lon axis
}

HOST_TEST(advance_axis_only_moves_forward_from_latitude_once)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    EXPECT_TRUE(coordinate_editor::advance_axis(state));
    EXPECT_TRUE(state.axis == coordinate_editor::Axis::Lon);
    EXPECT_FALSE(coordinate_editor::advance_axis(state)); // already on Lon
}

HOST_TEST(try_confirm_fails_before_the_longitude_axis_is_reached)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "50,-2");
    std::array<char, 32> text = {};
    EXPECT_FALSE(coordinate_editor::try_confirm(state, text.data(), text.size()));
}

HOST_TEST(try_confirm_formats_the_canonical_signed_text_on_success)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    (void)coordinate_editor::clear_buffer(state);
    (void)coordinate_editor::apply_digit(state, 5);
    (void)coordinate_editor::apply_digit(state, 0);
    (void)coordinate_editor::apply_dot(state);
    (void)coordinate_editor::apply_digit(state, 2);
    (void)coordinate_editor::toggle_hemisphere(state); // lat -> S
    (void)coordinate_editor::advance_axis(state);
    (void)coordinate_editor::clear_buffer(state);
    (void)coordinate_editor::apply_digit(state, 2);
    // lon stays 'E' (default)

    std::array<char, 32> text = {};
    EXPECT_TRUE(coordinate_editor::try_confirm(state, text.data(), text.size()));
    EXPECT_TRUE(std::strcmp(text.data(), "-50.2000,2.0000") == 0);
}

HOST_TEST(try_confirm_rejects_an_out_of_range_typed_magnitude)
{
    coordinate_editor::State state;
    (void)coordinate_editor::start(state, coordinate_editor::Field::AirTraffic, "");
    (void)coordinate_editor::clear_buffer(state);
    (void)coordinate_editor::apply_digit(state, 9);
    (void)coordinate_editor::apply_digit(state, 1); // latitude magnitude 91 -- out of range
    (void)coordinate_editor::advance_axis(state);

    std::array<char, 32> text = {};
    EXPECT_FALSE(coordinate_editor::try_confirm(state, text.data(), text.size()));
}

HOST_TEST(all_mutators_are_no_ops_when_no_edit_is_in_progress)
{
    coordinate_editor::State state;
    EXPECT_FALSE(coordinate_editor::apply_digit(state, 1));
    EXPECT_FALSE(coordinate_editor::apply_dot(state));
    EXPECT_FALSE(coordinate_editor::clear_buffer(state));
    EXPECT_FALSE(coordinate_editor::toggle_hemisphere(state));
    EXPECT_FALSE(coordinate_editor::advance_axis(state));
}
