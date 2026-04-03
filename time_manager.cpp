#include "time_manager.h"

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"

namespace {

/// @brief Monotonic period used to refresh the formatted clock text.
constexpr uint32_t kTimeTextUpdateIntervalMs = 1000;

TimeStatus g_status = {};
uint32_t g_last_ntp_epoch_utc = 0;
absolute_time_t g_last_ntp_sync_time = nil_time;
absolute_time_t g_next_time_text_update = nil_time;

struct DateTimeParts {
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
    const uint32_t day_seconds = 24u * 60u * 60u;
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
    parts.hour = static_cast<int>(seconds_of_day / 3600u);
    parts.minute = static_cast<int>((seconds_of_day % 3600u) / 60u);
    parts.second = static_cast<int>(seconds_of_day % 60u);
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
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return kDaysPerMonth[month - 1];
}

/// @brief Returns the weekday for the supplied Gregorian date.
int weekday_from_ymd(int year, int month, int day)
{
    static constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
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

/// @brief Returns whether UK daylight saving time is active for the given UTC epoch.
bool uk_daylight_saving_active_utc(uint32_t epoch_seconds)
{
    const DateTimeParts utc = unix_time_to_utc(epoch_seconds);
    if (utc.month < 3 || utc.month > 10) {
        return false;
    }
    if (utc.month > 3 && utc.month < 10) {
        return true;
    }

    if (utc.month == 3) {
        const int change_day = last_sunday_of_month(utc.year, 3);
        return utc.day > change_day || (utc.day == change_day && utc.hour >= 1);
    }

    const int change_day = last_sunday_of_month(utc.year, 10);
    return utc.day < change_day || (utc.day == change_day && utc.hour < 1);
}

/// @brief Returns the current local epoch derived from the last SNTP sync point.
uint32_t current_local_epoch_seconds()
{
    if (!g_status.synced || is_nil_time(g_last_ntp_sync_time)) {
        return 0;
    }

    const int64_t elapsed_us = absolute_time_diff_us(g_last_ntp_sync_time, get_absolute_time());
    const uint32_t elapsed_seconds = elapsed_us > 0 ? static_cast<uint32_t>(elapsed_us / 1000000) : 0;
    const uint32_t utc_epoch = g_last_ntp_epoch_utc + elapsed_seconds;
    return utc_epoch + (uk_daylight_saving_active_utc(utc_epoch) ? 3600u : 0u);
}

/// @brief Refreshes the user-facing `HH:MM` time text.
bool update_time_text()
{
    const char* previous_text = g_status.time_text.data();
    char next_text[sizeof(g_status.time_text)] = {};

    if (g_status.synced) {
        const DateTimeParts local = unix_time_to_utc(current_local_epoch_seconds());
        std::snprintf(next_text, sizeof(next_text), "%02d:%02d", local.hour, local.minute);
    }

    if (std::strncmp(previous_text, next_text, sizeof(g_status.time_text)) == 0) {
        return false;
    }

    g_status.time_text.fill('\0');
    std::snprintf(g_status.time_text.data(), g_status.time_text.size(), "%s", next_text);
    return true;
}

}  // namespace

extern "C" void merlinccu_set_ntp_time(uint32_t sec)
{
    g_last_ntp_epoch_utc = sec;
    g_last_ntp_sync_time = get_absolute_time();
    g_next_time_text_update = nil_time;
    g_status.synced = true;
}

namespace time_manager {

void init()
{
    g_status = {};
    g_status.synced = false;
    g_status.time_text.fill('\0');
    g_last_ntp_epoch_utc = 0;
    g_last_ntp_sync_time = nil_time;
    g_next_time_text_update = nil_time;
}

bool update()
{
    if (!g_status.synced) {
        return false;
    }

    if (is_nil_time(g_next_time_text_update) || absolute_time_diff_us(get_absolute_time(), g_next_time_text_update) <= 0) {
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

}  // namespace time_manager
