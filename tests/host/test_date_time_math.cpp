#include "date_time_math.h"

#include "test_framework.h"

using date_time_math::DateTimeParts;
using date_time_math::ParsedIsoDateTime;

HOST_TEST(unix_time_to_utc_epoch_zero_is_1970_01_01)
{
    const DateTimeParts parts = date_time_math::unix_time_to_utc(0U);
    EXPECT_EQ(parts.year, 1970);
    EXPECT_EQ(parts.month, 1);
    EXPECT_EQ(parts.day, 1);
    EXPECT_EQ(parts.hour, 0);
    EXPECT_EQ(parts.minute, 0);
    EXPECT_EQ(parts.second, 0);
}

HOST_TEST(unix_time_to_utc_and_utc_parts_to_epoch_round_trip)
{
    // Round-tripping avoids hardcoding external "known" epoch/date pairs that
    // would need independent verification -- if the two directions ever
    // disagree with each other, that's the bug this test exists to catch.
    const uint32_t sample_epochs[] = {0U, 1U, 86399U, 86400U, 1609459199U, 1700000000U,
                                      2000000000U, 4294967295U};
    for (uint32_t epoch : sample_epochs)
    {
        const DateTimeParts parts = date_time_math::unix_time_to_utc(epoch);
        uint32_t round_tripped = 0U;
        EXPECT_TRUE(date_time_math::utc_parts_to_epoch(parts, &round_tripped));
        EXPECT_EQ(round_tripped, epoch);
    }
}

HOST_TEST(civil_from_epoch_day_matches_days_from_civil_round_trip)
{
    const DateTimeParts epoch_zero = date_time_math::civil_from_epoch_day(0U);
    EXPECT_EQ(epoch_zero.year, 1970);
    EXPECT_EQ(epoch_zero.month, 1);
    EXPECT_EQ(epoch_zero.day, 1);

    const uint32_t sample_days[] = {1U, 30U, 365U, 366U, 20000U};
    for (uint32_t day : sample_days)
    {
        const DateTimeParts parts = date_time_math::civil_from_epoch_day(day);
        const int32_t round_tripped = date_time_math::days_from_civil(
            parts.year, static_cast<unsigned>(parts.month), static_cast<unsigned>(parts.day));
        EXPECT_EQ(round_tripped, static_cast<int32_t>(day));
    }
}

HOST_TEST(days_in_month_handles_leap_year_rules_correctly)
{
    // Divisible by 4 -> leap, except centuries, except divisible by 400.
    EXPECT_EQ(date_time_math::days_in_month(2024, 2), 29); // divisible by 4
    EXPECT_EQ(date_time_math::days_in_month(2023, 2), 28); // not divisible by 4
    EXPECT_EQ(date_time_math::days_in_month(1900, 2), 28); // century, not by 400
    EXPECT_EQ(date_time_math::days_in_month(2000, 2), 29); // century, by 400
    EXPECT_EQ(date_time_math::days_in_month(2024, 4), 30);
    EXPECT_EQ(date_time_math::days_in_month(2024, 1), 31);
}

HOST_TEST(utc_parts_to_epoch_rejects_invalid_calendar_fields)
{
    uint32_t out = 0U;
    DateTimeParts parts = {2024, 2, 30, 12, 0, 0}; // Feb 30 does not exist
    EXPECT_FALSE(date_time_math::utc_parts_to_epoch(parts, &out));

    parts = {2023, 2, 29, 12, 0, 0}; // 2023 is not a leap year
    EXPECT_FALSE(date_time_math::utc_parts_to_epoch(parts, &out));

    parts = {2024, 13, 1, 0, 0, 0}; // month 13
    EXPECT_FALSE(date_time_math::utc_parts_to_epoch(parts, &out));

    parts = {2024, 1, 1, 24, 0, 0}; // hour 24
    EXPECT_FALSE(date_time_math::utc_parts_to_epoch(parts, &out));

    parts = {1969, 12, 31, 23, 59, 59}; // before the epoch
    EXPECT_FALSE(date_time_math::utc_parts_to_epoch(parts, &out));
}

HOST_TEST(parse_iso8601_datetime_accepts_a_z_suffixed_timestamp)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_TRUE(date_time_math::parse_iso8601_datetime("2026-03-15T09:30:00Z", &parsed));
    EXPECT_EQ(parsed.parts.year, 2026);
    EXPECT_EQ(parsed.parts.month, 3);
    EXPECT_EQ(parsed.parts.day, 15);
    EXPECT_EQ(parsed.parts.hour, 9);
    EXPECT_EQ(parsed.parts.minute, 30);
    EXPECT_EQ(parsed.parts.second, 0);
    EXPECT_TRUE(parsed.has_explicit_offset);
    EXPECT_EQ(parsed.offset_seconds, 0);
}

HOST_TEST(parse_iso8601_datetime_accepts_an_explicit_positive_offset)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_TRUE(date_time_math::parse_iso8601_datetime("2026-03-15T09:30:00+05:30", &parsed));
    EXPECT_TRUE(parsed.has_explicit_offset);
    EXPECT_EQ(parsed.offset_seconds, (5 * 60 * 60) + (30 * 60));
}

HOST_TEST(parse_iso8601_datetime_accepts_an_explicit_negative_offset)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_TRUE(date_time_math::parse_iso8601_datetime("2026-03-15T09:30:00-04:00", &parsed));
    EXPECT_TRUE(parsed.has_explicit_offset);
    EXPECT_EQ(parsed.offset_seconds, -(4 * 60 * 60));
}

HOST_TEST(parse_iso8601_datetime_accepts_no_seconds_and_no_offset)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_TRUE(date_time_math::parse_iso8601_datetime("2026-03-15T09:30", &parsed));
    EXPECT_EQ(parsed.parts.second, 0);
    EXPECT_FALSE(parsed.has_explicit_offset);
}

HOST_TEST(parse_iso8601_datetime_accepts_fractional_seconds)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_TRUE(date_time_math::parse_iso8601_datetime("2026-03-15T09:30:00.123Z", &parsed));
    EXPECT_EQ(parsed.parts.second, 0);
    EXPECT_TRUE(parsed.has_explicit_offset);
}

HOST_TEST(parse_iso8601_datetime_rejects_malformed_input)
{
    ParsedIsoDateTime parsed = {};
    EXPECT_FALSE(date_time_math::parse_iso8601_datetime("not-a-date", &parsed));
    EXPECT_FALSE(date_time_math::parse_iso8601_datetime("2026-13-15T09:30:00Z", &parsed)); // month 13
    EXPECT_FALSE(date_time_math::parse_iso8601_datetime("2026-03-15", &parsed));           // no time part
    EXPECT_FALSE(date_time_math::parse_iso8601_datetime("2026/03/15T09:30:00Z", &parsed)); // wrong separators
    EXPECT_FALSE(date_time_math::parse_iso8601_datetime(nullptr, &parsed));
}

HOST_TEST(weekday_from_ymd_matches_the_known_y2k_anchor)
{
    // 2000-01-01 was a Saturday -- a well-known, independently verifiable
    // anchor point (this is what made the "Y2K" date itself a Saturday).
    // 0=Sunday in this function's convention, so Saturday is 6.
    EXPECT_EQ(date_time_math::weekday_from_ymd(2000, 1, 1), 6);
}

HOST_TEST(weekday_from_ymd_advances_by_one_for_each_consecutive_day)
{
    // Self-consistency check that does not depend on memorizing more dates:
    // seven consecutive days must cycle through all seven weekday values in
    // order and return to the start.
    const int start = date_time_math::weekday_from_ymd(2026, 2, 25);
    const int expected_sequence[7] = {start,           (start + 1) % 7, (start + 2) % 7,
                                      (start + 3) % 7, (start + 4) % 7, (start + 5) % 7,
                                      (start + 6) % 7};
    const int day_of_month[7] = {25, 26, 27, 28, 1, 2, 3}; // Feb 25-28, then Mar 1-3, 2026
    const int month_of_day[7] = {2, 2, 2, 2, 3, 3, 3};
    for (int i = 0; i < 7; ++i)
    {
        EXPECT_EQ(date_time_math::weekday_from_ymd(2026, month_of_day[i], day_of_month[i]),
                 expected_sequence[i]);
    }
}

HOST_TEST(european_dst_is_inactive_in_january_and_active_in_july)
{
    uint32_t epoch = 0U;
    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 1, 15, 12, 0, 0}, &epoch));
    EXPECT_FALSE(date_time_math::european_daylight_saving_active_utc(epoch));

    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 7, 15, 12, 0, 0}, &epoch));
    EXPECT_TRUE(date_time_math::european_daylight_saving_active_utc(epoch));
}

HOST_TEST(european_dst_transitions_exactly_at_0100_utc_on_the_last_sunday_of_march)
{
    const int change_day = date_time_math::last_sunday_of_month(2026, 3);
    uint32_t epoch = 0U;

    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 3, change_day, 0, 59, 59}, &epoch));
    EXPECT_FALSE(date_time_math::european_daylight_saving_active_utc(epoch));

    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 3, change_day, 1, 0, 0}, &epoch));
    EXPECT_TRUE(date_time_math::european_daylight_saving_active_utc(epoch));
}

HOST_TEST(european_dst_transitions_exactly_at_0100_utc_on_the_last_sunday_of_october)
{
    const int change_day = date_time_math::last_sunday_of_month(2026, 10);
    uint32_t epoch = 0U;

    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 10, change_day, 0, 59, 59}, &epoch));
    EXPECT_TRUE(date_time_math::european_daylight_saving_active_utc(epoch));

    EXPECT_TRUE(date_time_math::utc_parts_to_epoch({2026, 10, change_day, 1, 0, 0}, &epoch));
    EXPECT_FALSE(date_time_math::european_daylight_saving_active_utc(epoch));
}

HOST_TEST(base_utc_offset_seconds_matches_each_preset)
{
    EXPECT_EQ(date_time_math::base_utc_offset_seconds(TimeZoneSelection::EuropeLondon), 0);
    EXPECT_EQ(date_time_math::base_utc_offset_seconds(TimeZoneSelection::CentralEuropean), 3600);
    EXPECT_EQ(date_time_math::base_utc_offset_seconds(TimeZoneSelection::AtlanticStandard), -4 * 3600);
    EXPECT_EQ(date_time_math::base_utc_offset_seconds(TimeZoneSelection::GulfStandard), 4 * 3600);
}

HOST_TEST(uses_european_daylight_saving_is_scoped_to_the_expected_zones)
{
    EXPECT_TRUE(date_time_math::uses_european_daylight_saving(TimeZoneSelection::EuropeLondon));
    EXPECT_TRUE(date_time_math::uses_european_daylight_saving(TimeZoneSelection::CentralEuropean));
    EXPECT_FALSE(date_time_math::uses_european_daylight_saving(TimeZoneSelection::ArabiaStandard));
    EXPECT_FALSE(date_time_math::uses_european_daylight_saving(TimeZoneSelection::AtlanticStandard));
}
