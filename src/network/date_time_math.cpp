#include "date_time_math.h"

namespace date_time_math
{

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

DateTimeParts civil_from_epoch_day(uint32_t epoch_day)
{
    constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
    return unix_time_to_utc(epoch_day * kSecondsPerDay);
}

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

bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

int days_in_month(int year, int month)
{
    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDaysPerMonth[month - 1];
}

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

namespace
{

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

} // namespace

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

int weekday_from_ymd(int year, int month, int day)
{
    static constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
    {
        --year;
    }
    return (year + year / 4 - year / 100 + year / 400 + kMonthOffsets[month - 1] + day) % 7;
}

int last_sunday_of_month(int year, int month)
{
    const int last_day = days_in_month(year, month);
    return last_day - weekday_from_ymd(year, month, last_day);
}

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

} // namespace date_time_math
