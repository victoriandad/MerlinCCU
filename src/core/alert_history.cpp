#include "alert_history.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "config_persistence.h"
#include "date_time_math.h"

// The history lives in PSRAM on boards that have it. psram.h needs the Pico
// SDK, which the host tests do not have; there it is ordinary memory.
#if __has_include("pico.h")
#include "psram.h"
#else
#define MERLIN_PSRAM_BSS
#endif

namespace alert_history
{

namespace
{

constexpr uint32_t kMinutesPerHour = 60U;
constexpr uint32_t kMinutesPerDay = 24U * kMinutesPerHour;
constexpr uint8_t kMaxCount = 99U;

MERLIN_PSRAM_BSS Image g_image;

bool g_new_pending = false;
bool g_fold_pending = false;
bool g_ever_saved = false;
uint32_t g_last_save_ms = 0U;

const Entry kEmptyEntry = {};

uint32_t entries_crc(const Image& image)
{
    return config_persistence::crc32(reinterpret_cast<const uint8_t*>(image.entries.data()),
                                     sizeof(image.entries));
}

/// @brief Takes the entry at `from` out and puts `entry` at the front,
/// moving the ones before it down one place.
void move_to_front(size_t from, const Entry& entry)
{
    for (size_t i = from; i > 0U; --i)
    {
        g_image.entries[i] = g_image.entries[i - 1U];
    }
    g_image.entries[0] = entry;
}

/// @brief Index of the newest entry for `code`, or kCapacity if none.
size_t newest_for(uint8_t code)
{
    for (size_t i = 0U; i < g_image.entry_count; ++i)
    {
        if (g_image.entries[i].code == code)
        {
            return i;
        }
    }
    return kCapacity;
}

/// @brief True when `minute` ends within the fold window of `raised`.
bool within_fold_window(uint32_t raised, uint32_t minute)
{
    return raised != 0U && minute != 0U && minute >= raised && minute - raised < kFoldWindowMinutes;
}

bool same_day(uint32_t a, uint32_t b)
{
    return a != 0U && b != 0U && a / kMinutesPerDay == b / kMinutesPerDay;
}

/// @brief `24 Nov`.
void format_date(uint32_t minute, char* out, size_t out_size)
{
    const date_time_math::DateTimeParts date =
        date_time_math::civil_from_epoch_day(minute / kMinutesPerDay);
    const size_t month = static_cast<size_t>(std::clamp(date.month, 1, 12) - 1);
    std::snprintf(out, out_size, "%d %s", date.day, date_time_math::kMonthAbbreviations[month]);
}

/// @brief A time, with its date when it is not on `today_minute`'s day.
void format_when(uint32_t minute, uint32_t today_minute, char* out, size_t out_size)
{
    if (minute != 0U && today_minute != 0U && !same_day(minute, today_minute))
    {
        format_date_time(minute, out, out_size);
        return;
    }
    format_time(minute, out, out_size);
}

const char* severity_text(AlertSeverity severity)
{
    switch (severity)
    {
    case AlertSeverity::Alert:
        return "Alert";
    case AlertSeverity::Warning:
        return "Warning";
    case AlertSeverity::Message:
    case AlertSeverity::None:
        break;
    }
    return "Message";
}

} // namespace

uint32_t local_minute(const TimeStatus& time)
{
    const char* text = time.time_text.data();
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!time.synced || time.local_epoch_day == 0U || !digit(text[0]) || !digit(text[1]) ||
        text[2] != ':' || !digit(text[3]) || !digit(text[4]))
    {
        return 0U;
    }
    const uint32_t hour = static_cast<uint32_t>(((text[0] - '0') * 10) + (text[1] - '0'));
    const uint32_t minute = static_cast<uint32_t>(((text[3] - '0') * 10) + (text[4] - '0'));
    return (time.local_epoch_day * kMinutesPerDay) + (hour * kMinutesPerHour) + minute;
}

void reset()
{
    g_image = {};
    g_image.next_entry_sequence = 1U;
    g_new_pending = false;
    g_fold_pending = false;
    g_ever_saved = false;
    g_last_save_ms = 0U;
}

void record(const ActiveAlert& alert, Ending ending, uint32_t now_minute)
{
    if (g_image.next_entry_sequence == 0U)
    {
        g_image.next_entry_sequence = 1U;
    }

    const size_t existing = newest_for(alert.code);
    const bool fold = existing < g_image.entry_count &&
                      within_fold_window(g_image.entries[existing].raised_minute, now_minute);

    Entry updated = fold ? g_image.entries[existing] : Entry{};
    updated.sequence = g_image.next_entry_sequence++;
    updated.code = alert.code;
    updated.severity = alert.severity;
    updated.ending = ending;
    updated.ended_minute = now_minute;
    if (alert.ignored)
    {
        updated.ignored = true;
        updated.ignored_minute = alert.ignored_minute;
    }
    updated.summary = alert.summary;
    updated.summary.back() = '\0';
    std::snprintf(updated.detail.data(), updated.detail.size(), "%s", alert.detail.data());

    if (fold)
    {
        updated.count = static_cast<uint8_t>(std::min<unsigned>(updated.count + 1U, kMaxCount));
        move_to_front(existing, updated);
        g_fold_pending = true;
        return;
    }

    updated.count = 1U;
    updated.raised_minute = alert.raised_minute;
    const size_t last = std::min<size_t>(g_image.entry_count, kCapacity - 1U);
    move_to_front(last, updated);
    g_image.entry_count =
        static_cast<uint16_t>(std::min<size_t>(g_image.entry_count + 1U, kCapacity));
    g_new_pending = true;
}

void clear_all()
{
    const uint32_t next_sequence = g_image.next_entry_sequence;
    g_image.entries = {};
    g_image.entry_count = 0U;
    g_image.next_entry_sequence = next_sequence;
    g_new_pending = true;
}

uint8_t count()
{
    return static_cast<uint8_t>(std::min<size_t>(g_image.entry_count, kCapacity));
}

const Entry& entry(uint8_t index)
{
    return index < count() ? g_image.entries[index] : kEmptyEntry;
}

const Entry* find(uint32_t sequence)
{
    for (uint8_t i = 0U; i < count(); ++i)
    {
        if (g_image.entries[i].sequence == sequence)
        {
            return &g_image.entries[i];
        }
    }
    return nullptr;
}

uint8_t page_count()
{
    return list_page_count(count(), kAlertsPerPage);
}

void format_time(uint32_t minute, char* out, size_t out_size)
{
    if (minute == 0U)
    {
        std::snprintf(out, out_size, "--:--");
        return;
    }
    const uint32_t of_day = minute % kMinutesPerDay;
    std::snprintf(out, out_size, "%02u:%02u", static_cast<unsigned>(of_day / kMinutesPerHour),
                  static_cast<unsigned>(of_day % kMinutesPerHour));
}

void format_date_time(uint32_t minute, char* out, size_t out_size)
{
    if (minute == 0U)
    {
        std::snprintf(out, out_size, "--:--");
        return;
    }
    char date[12] = {};
    char time[8] = {};
    format_date(minute, date, sizeof(date));
    format_time(minute, time, sizeof(time));
    std::snprintf(out, out_size, "%s %s", date, time);
}

void format_key_value(const Entry& entry, uint32_t today_minute, char* out, size_t out_size)
{
    const uint32_t anchor = entry.raised_minute != 0U ? entry.raised_minute : entry.ended_minute;
    char start[12] = {};
    if (anchor != 0U && today_minute != 0U && !same_day(anchor, today_minute))
    {
        format_date(anchor, start, sizeof(start));
        if (entry.count > 1U)
        {
            std::snprintf(out, out_size, "[%s x%u]", start, static_cast<unsigned>(entry.count));
            return;
        }
        char time[8] = {};
        format_time(anchor, time, sizeof(time));
        std::snprintf(out, out_size, "[%s %s]", start, time);
        return;
    }

    format_time(anchor, start, sizeof(start));
    if (entry.count > 1U)
    {
        std::snprintf(out, out_size, "[%s x%u]", start, static_cast<unsigned>(entry.count));
        return;
    }
    if (entry.raised_minute == 0U || entry.ended_minute == 0U)
    {
        std::snprintf(out, out_size, "[%s]", start);
        return;
    }
    char end[8] = {};
    format_time(entry.ended_minute, end, sizeof(end));
    std::snprintf(out, out_size, "[%s-%s]", start, end);
}

void format_detail(const Entry& entry, uint32_t today_minute, char* out, size_t out_size)
{
    char raised[20] = {};
    char ended[20] = {};
    char ignored[48] = {};
    char repeats[48] = {};
    format_when(entry.raised_minute, today_minute, raised, sizeof(raised));
    format_when(entry.ended_minute, today_minute, ended, sizeof(ended));
    if (entry.ignored)
    {
        char when[20] = {};
        format_when(entry.ignored_minute, today_minute, when, sizeof(when));
        std::snprintf(ignored, sizeof(ignored), "Ignored: %s\n", when);
    }
    if (entry.count > 1U)
    {
        std::snprintf(repeats, sizeof(repeats), "Ended %u times within the hour.\n",
                      static_cast<unsigned>(entry.count));
    }
    std::snprintf(out, out_size, "%s (%s)\nRaised: %s\n%s%s: %s\n%s\n%s", entry.summary.data(),
                  severity_text(entry.severity), raised, ignored,
                  entry.ending == Ending::Accepted ? "Accepted" : "Cleared", ended, repeats,
                  entry.detail.data());
}

const Image& image_for_save(uint32_t save_sequence)
{
    g_image.magic = kImageMagic;
    g_image.version = kImageVersion;
    g_image.save_sequence = save_sequence;
    g_image.payload_size = sizeof(g_image.entries);
    g_image.crc32 = entries_crc(g_image);
    return g_image;
}

bool image_valid(const Image& image)
{
    return image.magic == kImageMagic && image.version == kImageVersion &&
           image.entry_count <= kCapacity && image.payload_size == sizeof(image.entries) &&
           image.crc32 == entries_crc(image);
}

bool restore(const Image& image)
{
    if (!image_valid(image))
    {
        return false;
    }
    g_image = image;
    if (g_image.next_entry_sequence == 0U)
    {
        g_image.next_entry_sequence = 1U;
    }
    return true;
}

bool save_due(uint32_t now_ms)
{
    if (!g_new_pending && !g_fold_pending)
    {
        return false;
    }
    if (!g_ever_saved)
    {
        return true;
    }
    const uint32_t elapsed = now_ms - g_last_save_ms;
    return elapsed >= (g_new_pending ? kNewEntrySaveDelayMs : kFoldSaveDelayMs);
}

void mark_saved(uint32_t now_ms)
{
    g_new_pending = false;
    g_fold_pending = false;
    g_ever_saved = true;
    g_last_save_ms = now_ms;
}

} // namespace alert_history
