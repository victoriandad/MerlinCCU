#include "time_manager.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "config_manager.h"
#include "date_time_math.h"
#include "pico/stdlib.h"

namespace
{

using date_time_math::DateTimeParts;
using date_time_math::ParsedIsoDateTime;
using date_time_math::base_utc_offset_seconds;
using date_time_math::days_in_month;
using date_time_math::european_daylight_saving_active_utc;
using date_time_math::parse_iso8601_datetime;
using date_time_math::unix_time_to_utc;
using date_time_math::utc_parts_to_epoch;
using date_time_math::uses_european_daylight_saving;
using date_time_math::weekday_from_ymd;

/// @brief Monotonic period used to refresh the formatted clock text.
constexpr uint32_t kTimeTextUpdateIntervalMs = 1000;

TimeStatus g_status = {};
uint32_t g_last_ntp_epoch_utc = 0;
absolute_time_t g_last_ntp_sync_time = nil_time;
absolute_time_t g_next_time_text_update = nil_time;

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
    std::array<char, sizeof(g_status.time_text)> next_text = {};
    std::array<char, sizeof(g_status.date_text)> next_date = {};
    uint32_t next_epoch_day = 0U;
    uint8_t next_weekday = kInvalidWeekdayIndex;

    // The formatted string is rebuilt from the stored sync point each time so
    // the UI stays monotonic without needing a separate RTC subsystem.
    if (g_status.synced)
    {
        constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
        const uint32_t local_epoch_seconds = current_local_epoch_seconds();
        const DateTimeParts local = unix_time_to_utc(local_epoch_seconds);
        std::snprintf(next_text.data(), next_text.size(), "%02d:%02d", local.hour, local.minute);
        std::snprintf(next_date.data(), next_date.size(), "%04d-%02d-%02d", local.year, local.month,
                      local.day);
        next_epoch_day = local_epoch_seconds / kSecondsPerDay;
        next_weekday = static_cast<uint8_t>(weekday_from_ymd(local.year, local.month, local.day));
    }

    if (std::strncmp(previous_text, next_text.data(), sizeof(g_status.time_text)) == 0 &&
        std::strncmp(previous_date, next_date.data(), sizeof(g_status.date_text)) == 0 &&
        previous_epoch_day == next_epoch_day &&
        previous_weekday == next_weekday)
    {
        return false;
    }

    g_status.time_text.fill('\0');
    std::snprintf(g_status.time_text.data(), g_status.time_text.size(), "%s", next_text.data());
    g_status.date_text.fill('\0');
    std::snprintf(g_status.date_text.data(), g_status.date_text.size(), "%s", next_date.data());
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
