#include "time_manager.h"

#include <cstdio>
#include <cstring>

#include "config_manager.h"
#include "pico/stdlib.h"

namespace
{

/// @brief Monotonic period used to refresh the formatted clock text.
constexpr uint32_t kTimeTextUpdateIntervalMs = 1000;

TimeStatus g_status = {};
uint32_t g_last_ntp_epoch_utc = 0;
absolute_time_t g_last_ntp_sync_time = nil_time;
absolute_time_t g_next_time_text_update = nil_time;

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
DateTimeParts unix_time_to_utc(uint32_t epoch_seconds)
{
    const uint32_t day_seconds = 24U * 60U * 60U;
    const uint32_t days = epoch_seconds / day_seconds;
    const uint32_t seconds_of_day = epoch_seconds % day_seconds;

    int z = static_cast<int>(days) + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp + (mp < 10 ? 3 : -9);
    year += (month <= 2);

    DateTimeParts parts = {};
    parts.year = year;
    parts.month = static_cast<int>(month);
    parts.day = static_cast<int>(day);
    parts.hour = static_cast<int>(seconds_of_day / 3600U);
    parts.minute = static_cast<int>((seconds_of_day % 3600U) / 60U);
    parts.second = static_cast<int>(seconds_of_day % 60U);
    return parts;
}

/// @brief Converts Gregorian date fields to days since Unix epoch.
/// @details This is the inverse of `unix_time_to_utc()` and lets offset-bearing
/// provider timestamps be converted without pulling in libc time-zone support.
int32_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2U ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - (era * 400));
    const unsigned adjusted_month = month + (month > 2U ? -3U : 9U);
    const unsigned doy = ((153U * adjusted_month) + 2U) / 5U + day - 1U;
    const unsigned doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;
    return (era * 146097) + static_cast<int32_t>(doe) - 719468;
}

/// @brief Returns whether the given Gregorian year is a leap year.
bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

/// @brief Returns the number of days in the requested month.
int days_in_month(int year, int month)
{
    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDaysPerMonth[month - 1];
}

/// @brief Builds a UTC epoch from broken-down UTC calendar fields.
bool utc_parts_to_epoch(const DateTimeParts& parts, uint32_t* out_epoch_seconds)
{
    if (out_epoch_seconds == nullptr || parts.year < 1970 || parts.month < 1 || parts.month > 12 ||
        parts.day < 1 || parts.day > days_in_month(parts.year, parts.month) || parts.hour < 0 ||
        parts.hour > 23 || parts.minute < 0 || parts.minute > 59 || parts.second < 0 ||
        parts.second > 59)
    {
        return false;
    }

    constexpr int64_t kSecondsPerDay = 24LL * 60LL * 60LL;
    const int64_t days = static_cast<int64_t>(days_from_civil(
        parts.year, static_cast<unsigned>(parts.month), static_cast<unsigned>(parts.day)));
    const int64_t epoch = (days * kSecondsPerDay) +
                          (static_cast<int64_t>(parts.hour) * 60LL * 60LL) +
                          (static_cast<int64_t>(parts.minute) * 60LL) + parts.second;
    if (epoch < 0 || epoch > UINT32_MAX)
    {
        return false;
    }

    *out_epoch_seconds = static_cast<uint32_t>(epoch);
    return true;
}

/// @brief Parses exactly `digits` decimal digits and advances `cursor`.
bool parse_fixed_digits(const char** cursor, int digits, int* out_value)
{
    if (cursor == nullptr || *cursor == nullptr || out_value == nullptr || digits <= 0)
    {
        return false;
    }

    int value = 0;
    const char* scan = *cursor;
    for (int i = 0; i < digits; ++i)
    {
        if (scan[i] < '0' || scan[i] > '9')
        {
            return false;
        }
        value = (value * 10) + (scan[i] - '0');
    }

    *cursor = scan + digits;
    *out_value = value;
    return true;
}

/// @brief Consumes one exact separator character from a parser cursor.
bool consume_char(const char** cursor, char expected)
{
    if (cursor == nullptr || *cursor == nullptr || **cursor != expected)
    {
        return false;
    }

    ++(*cursor);
    return true;
}

/// @brief Parses ISO-8601 timestamps used by Home Assistant and weather APIs.
bool parse_iso8601_datetime(const char* iso_datetime, ParsedIsoDateTime* out_datetime)
{
    if (iso_datetime == nullptr || out_datetime == nullptr)
    {
        return false;
    }

    const char* cursor = iso_datetime;
    ParsedIsoDateTime parsed = {};
    if (!parse_fixed_digits(&cursor, 4, &parsed.parts.year) || !consume_char(&cursor, '-') ||
        !parse_fixed_digits(&cursor, 2, &parsed.parts.month) || !consume_char(&cursor, '-') ||
        !parse_fixed_digits(&cursor, 2, &parsed.parts.day))
    {
        return false;
    }
    if (*cursor != 'T' && *cursor != 't' && *cursor != ' ')
    {
        return false;
    }
    ++cursor;

    if (!parse_fixed_digits(&cursor, 2, &parsed.parts.hour) || !consume_char(&cursor, ':') ||
        !parse_fixed_digits(&cursor, 2, &parsed.parts.minute))
    {
        return false;
    }

    parsed.parts.second = 0;
    if (*cursor == ':')
    {
        ++cursor;
        if (!parse_fixed_digits(&cursor, 2, &parsed.parts.second))
        {
            return false;
        }
    }

    if (parsed.parts.month < 1 || parsed.parts.month > 12 || parsed.parts.day < 1 ||
        parsed.parts.day > days_in_month(parsed.parts.year, parsed.parts.month) ||
        parsed.parts.hour < 0 || parsed.parts.hour > 23 || parsed.parts.minute < 0 ||
        parsed.parts.minute > 59 || parsed.parts.second < 0 || parsed.parts.second > 59)
    {
        return false;
    }

    while (*cursor >= '0' && *cursor <= '9')
    {
        ++cursor;
    }
    if (*cursor == '.')
    {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9')
        {
            ++cursor;
        }
    }

    if (*cursor == 'Z' || *cursor == 'z')
    {
        parsed.has_explicit_offset = true;
        parsed.offset_seconds = 0;
        *out_datetime = parsed;
        return true;
    }

    if (*cursor == '+' || *cursor == '-')
    {
        const int sign = *cursor == '+' ? 1 : -1;
        ++cursor;

        int offset_hours = 0;
        int offset_minutes = 0;
        if (!parse_fixed_digits(&cursor, 2, &offset_hours))
        {
            return false;
        }
        if (*cursor == ':')
        {
            ++cursor;
        }
        if (!parse_fixed_digits(&cursor, 2, &offset_minutes))
        {
            return false;
        }
        if (offset_hours > 23 || offset_minutes > 59)
        {
            return false;
        }

        parsed.has_explicit_offset = true;
        parsed.offset_seconds = sign * ((offset_hours * 60 * 60) + (offset_minutes * 60));
    }

    *out_datetime = parsed;
    return true;
}

/// @brief Returns the weekday for the supplied Gregorian date.
int weekday_from_ymd(int year, int month, int day)
{
    static constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
    {
        --year;
    }
    return (year + year / 4 - year / 100 + year / 400 + kMonthOffsets[month - 1] + day) % 7;
}

/// @brief Returns the day-of-month for the last Sunday in the requested month.
int last_sunday_of_month(int year, int month)
{
    const int last_day = days_in_month(year, month);
    return last_day - weekday_from_ymd(year, month, last_day);
}

/// @brief Returns whether European daylight saving time is active at a UTC epoch.
/// @details The affected presets all follow the common EU transition instants:
/// 01:00 UTC on the last Sunday in March and October.
bool european_daylight_saving_active_utc(uint32_t epoch_seconds)
{
    const DateTimeParts utc = unix_time_to_utc(epoch_seconds);
    if (utc.month < 3 || utc.month > 10)
    {
        return false;
    }
    if (utc.month > 3 && utc.month < 10)
    {
        return true;
    }

    if (utc.month == 3)
    {
        const int change_day = last_sunday_of_month(utc.year, 3);
        return utc.day > change_day || (utc.day == change_day && utc.hour >= 1);
    }

    const int change_day = last_sunday_of_month(utc.year, 10);
    return utc.day < change_day || (utc.day == change_day && utc.hour < 1);
}

/// @brief Returns the base UTC offset, in seconds, for one configured time zone.
int32_t base_utc_offset_seconds(TimeZoneSelection zone)
{
    constexpr int32_t kSecondsPerHour = 60 * 60;

    switch (zone)
    {
    case TimeZoneSelection::AtlanticStandard:
        return -4 * kSecondsPerHour;
    case TimeZoneSelection::ArgentinaStandard:
        return -3 * kSecondsPerHour;
    case TimeZoneSelection::SouthGeorgia:
        return -2 * kSecondsPerHour;
    case TimeZoneSelection::Azores:
        return -1 * kSecondsPerHour;
    case TimeZoneSelection::EuropeLondon:
        return 0;
    case TimeZoneSelection::CentralEuropean:
        return 1 * kSecondsPerHour;
    case TimeZoneSelection::EasternEuropean:
        return 2 * kSecondsPerHour;
    case TimeZoneSelection::ArabiaStandard:
        return 3 * kSecondsPerHour;
    case TimeZoneSelection::GulfStandard:
        return 4 * kSecondsPerHour;
    }

    return 0;
}

/// @brief Returns whether the selected zone uses the shared European DST rules.
bool uses_european_daylight_saving(TimeZoneSelection zone)
{
    switch (zone)
    {
    case TimeZoneSelection::Azores:
    case TimeZoneSelection::EuropeLondon:
    case TimeZoneSelection::CentralEuropean:
    case TimeZoneSelection::EasternEuropean:
        return true;
    case TimeZoneSelection::AtlanticStandard:
    case TimeZoneSelection::ArgentinaStandard:
    case TimeZoneSelection::SouthGeorgia:
    case TimeZoneSelection::ArabiaStandard:
    case TimeZoneSelection::GulfStandard:
        return false;
    }

    return false;
}

/// @brief Returns the configured display-zone offset for a UTC instant.
int32_t display_utc_offset_seconds(uint32_t utc_epoch)
{
    const TimeZoneSelection zone = config_manager::settings().time_zone;
    int32_t utc_offset_seconds = base_utc_offset_seconds(zone);
    if (uses_european_daylight_saving(zone) && european_daylight_saving_active_utc(utc_epoch))
    {
        utc_offset_seconds += 3600;
    }
    return utc_offset_seconds;
}

/// @brief Returns the current local epoch derived from the last SNTP sync point.
uint32_t current_local_epoch_seconds()
{
    if (!g_status.synced || is_nil_time(g_last_ntp_sync_time))
    {
        return 0;
    }

    const int64_t elapsed_us = absolute_time_diff_us(g_last_ntp_sync_time, get_absolute_time());
    const uint32_t elapsed_seconds =
        elapsed_us > 0 ? static_cast<uint32_t>(elapsed_us / 1000000) : 0;
    const uint32_t utc_epoch = g_last_ntp_epoch_utc + elapsed_seconds;
    const int32_t utc_offset_seconds = display_utc_offset_seconds(utc_epoch);

    const int64_t local_epoch = static_cast<int64_t>(utc_epoch) + utc_offset_seconds;
    return local_epoch > 0 ? static_cast<uint32_t>(local_epoch) : 0U;
}

/// @brief Refreshes the user-facing `HH:MM` time text.
bool update_time_text()
{
    const char* previous_text = g_status.time_text.data();
    const char* previous_date = g_status.date_text.data();
    const uint32_t previous_epoch_day = g_status.local_epoch_day;
    const uint8_t previous_weekday = g_status.weekday_index;
    char next_text[sizeof(g_status.time_text)] = {};
    char next_date[sizeof(g_status.date_text)] = {};
    uint32_t next_epoch_day = 0U;
    uint8_t next_weekday = kInvalidWeekdayIndex;

    // The formatted string is rebuilt from the stored sync point each time so
    // the UI stays monotonic without needing a separate RTC subsystem.
    if (g_status.synced)
    {
        constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
        const uint32_t local_epoch_seconds = current_local_epoch_seconds();
        const DateTimeParts local = unix_time_to_utc(local_epoch_seconds);
        std::snprintf(next_text, sizeof(next_text), "%02d:%02d", local.hour, local.minute);
        std::snprintf(next_date, sizeof(next_date), "%04d-%02d-%02d", local.year, local.month,
                      local.day);
        next_epoch_day = local_epoch_seconds / kSecondsPerDay;
        next_weekday = static_cast<uint8_t>(weekday_from_ymd(local.year, local.month, local.day));
    }

    if (std::strncmp(previous_text, next_text, sizeof(g_status.time_text)) == 0 &&
        std::strncmp(previous_date, next_date, sizeof(g_status.date_text)) == 0 &&
        previous_epoch_day == next_epoch_day &&
        previous_weekday == next_weekday)
    {
        return false;
    }

    g_status.time_text.fill('\0');
    std::snprintf(g_status.time_text.data(), g_status.time_text.size(), "%s", next_text);
    g_status.date_text.fill('\0');
    std::snprintf(g_status.date_text.data(), g_status.date_text.size(), "%s", next_date);
    g_status.local_epoch_day = next_epoch_day;
    g_status.weekday_index = next_weekday;
    return true;
}

} // namespace

extern "C" void merlinccu_set_ntp_time(uint32_t sec)
{
    // SNTP hands us UTC seconds, while the display path wants a rolling local
    // clock, so we store both the epoch and the moment it was received.
    g_last_ntp_epoch_utc = sec;
    g_last_ntp_sync_time = get_absolute_time();
    g_next_time_text_update = nil_time;
    g_status.synced = true;
}

namespace time_manager
{

/// @brief Resets the time manager to an unsynchronized startup state.
void init()
{
    // Time starts unsynced on every boot so stale values are never carried
    // forward if network time is unavailable later.
    g_status = {};
    g_status.synced = false;
    g_status.time_text.fill('\0');
    g_status.date_text.fill('\0');
    g_status.local_epoch_day = 0U;
    g_status.weekday_index = kInvalidWeekdayIndex;
    g_last_ntp_epoch_utc = 0;
    g_last_ntp_sync_time = nil_time;
    g_next_time_text_update = nil_time;
}

/// @brief Refreshes the cached display time text when its update period expires.
bool update()
{
    if (!g_status.synced)
    {
        return false;
    }

    // The text is only regenerated once per second because the UI only shows
    // `HH:MM`, so anything faster would just waste redraw work.
    if (is_nil_time(g_next_time_text_update) ||
        absolute_time_diff_us(get_absolute_time(), g_next_time_text_update) <= 0)
    {
        const bool changed = update_time_text();
        g_next_time_text_update = make_timeout_time_ms(kTimeTextUpdateIntervalMs);
        return changed;
    }

    return false;
}

const TimeStatus& status()
{
    return g_status;
}

bool format_local_time_from_iso8601(const char* iso_datetime, char* out, size_t out_size)
{
    if (iso_datetime == nullptr || out == nullptr || out_size < 6)
    {
        return false;
    }

    ParsedIsoDateTime parsed = {};
    if (!parse_iso8601_datetime(iso_datetime, &parsed))
    {
        return false;
    }

    if (!parsed.has_explicit_offset)
    {
        std::snprintf(out, out_size, "%02d:%02d", parsed.parts.hour, parsed.parts.minute);
        return true;
    }

    uint32_t source_epoch = 0;
    if (!utc_parts_to_epoch(parsed.parts, &source_epoch))
    {
        return false;
    }

    const int64_t utc_epoch = static_cast<int64_t>(source_epoch) - parsed.offset_seconds;
    if (utc_epoch < 0 || utc_epoch > UINT32_MAX)
    {
        return false;
    }

    const int64_t local_epoch =
        utc_epoch + display_utc_offset_seconds(static_cast<uint32_t>(utc_epoch));
    if (local_epoch < 0 || local_epoch > UINT32_MAX)
    {
        return false;
    }

    const DateTimeParts local = unix_time_to_utc(static_cast<uint32_t>(local_epoch));
    std::snprintf(out, out_size, "%02d:%02d", local.hour, local.minute);
    return true;
}

} // namespace time_manager
