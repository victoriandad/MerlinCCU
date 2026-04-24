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
    const TimeZoneSelection zone = config_manager::settings().time_zone;
    int32_t utc_offset_seconds = base_utc_offset_seconds(zone);
    if (uses_european_daylight_saving(zone) && european_daylight_saving_active_utc(utc_epoch))
    {
        utc_offset_seconds += 3600;
    }

    const int64_t local_epoch = static_cast<int64_t>(utc_epoch) + utc_offset_seconds;
    return local_epoch > 0 ? static_cast<uint32_t>(local_epoch) : 0U;
}

/// @brief Refreshes the user-facing `HH:MM` time text.
bool update_time_text()
{
    const char* previous_text = g_status.time_text.data();
    char next_text[sizeof(g_status.time_text)] = {};

    // The formatted string is rebuilt from the stored sync point each time so
    // the UI stays monotonic without needing a separate RTC subsystem.
    if (g_status.synced)
    {
        const DateTimeParts local = unix_time_to_utc(current_local_epoch_seconds());
        std::snprintf(next_text, sizeof(next_text), "%02d:%02d", local.hour, local.minute);
    }

    if (std::strncmp(previous_text, next_text, sizeof(g_status.time_text)) == 0)
    {
        return false;
    }

    g_status.time_text.fill('\0');
    std::snprintf(g_status.time_text.data(), g_status.time_text.size(), "%s", next_text);
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

} // namespace time_manager
