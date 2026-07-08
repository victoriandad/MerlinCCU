#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "console_model.h"

/// @brief Pure calendar/ISO-8601/DST math shared between the firmware and host
/// tests. No dependency on absolute_time_t, SNTP sync state, or any other
/// runtime state -- every function here is a deterministic function of its
/// arguments, which is exactly what makes date-boundary and DST-transition
/// bugs (the kind that only show up once or twice a year) worth testing
/// directly instead of only via manual bring-up.
namespace date_time_math
{

/// @brief Broken-down UTC calendar fields.
struct DateTimeParts
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

/// @brief Parsed ISO-8601 timestamp fields used for timezone-aware display.
struct ParsedIsoDateTime
{
    DateTimeParts parts;
    bool has_explicit_offset;
    int32_t offset_seconds;
};

/// @brief Converts a Unix UTC epoch into broken-down UTC calendar fields.
DateTimeParts unix_time_to_utc(uint32_t epoch_seconds);

/// @brief Converts a day count since the Unix epoch into UTC calendar fields.
/// @details `local_epoch_day` and Pinter event-day stamps are day counts, not
/// second-precision timestamps; this is the inverse-friendly counterpart to
/// `days_from_civil()` for that representation, built on `unix_time_to_utc()`
/// so there is one Howard civil-from-days implementation, not two.
DateTimeParts civil_from_epoch_day(uint32_t epoch_day);

/// @brief Three-letter month abbreviations, indexed 0 = January.
inline constexpr std::array<const char*, 12> kMonthAbbreviations = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/// @brief Converts Gregorian date fields to days since the Unix epoch.
/// @details This is the inverse of `unix_time_to_utc()` and lets offset-bearing
/// provider timestamps be converted without pulling in libc time-zone support.
int32_t days_from_civil(int year, unsigned month, unsigned day);

/// @brief Returns whether the given Gregorian year is a leap year.
bool is_leap_year(int year);

/// @brief Returns the number of days in the requested month.
int days_in_month(int year, int month);

/// @brief Builds a UTC epoch from broken-down UTC calendar fields.
bool utc_parts_to_epoch(const DateTimeParts& parts, uint32_t* out_epoch_seconds);

/// @brief Parses ISO-8601 timestamps used by Home Assistant and weather APIs.
bool parse_iso8601_datetime(const char* iso_datetime, ParsedIsoDateTime* out_datetime);

/// @brief Returns the weekday (0=Sunday) for the supplied Gregorian date.
int weekday_from_ymd(int year, int month, int day);

/// @brief Returns the day-of-month for the last Sunday in the requested month.
int last_sunday_of_month(int year, int month);

/// @brief Returns whether European daylight saving time is active at a UTC epoch.
/// @details The affected presets all follow the common EU transition instants:
/// 01:00 UTC on the last Sunday in March and October.
bool european_daylight_saving_active_utc(uint32_t epoch_seconds);

/// @brief Returns the base (non-DST) UTC offset, in seconds, for one configured time zone.
int32_t base_utc_offset_seconds(TimeZoneSelection zone);

/// @brief Returns whether the selected zone uses the shared European DST rules.
bool uses_european_daylight_saving(TimeZoneSelection zone);

} // namespace date_time_math
