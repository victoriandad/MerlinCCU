#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "console_model.h"

/// @brief Alerts that have ended, newest first (#203).
/// @details An entry is added when an alert's condition clears or the
/// operator accepts it. The same alert ending again within the hour of its
/// entry raises that entry's count rather than adding another, so a flapping
/// alert takes one key. The newest kCapacity entries are kept; the oldest
/// drops off when a new one arrives.
///
/// Times are "local minutes": minutes since the Unix epoch in local time,
/// from TimeStatus. 0 means the clock had not synced, shown as `--:--`.
///
/// The whole history is one flash image (Image), kept in PSRAM on boards
/// that have it, and saved by alert_history_store with the two-slot pattern
/// the settings and Pinter stores use. This module decides what to keep and
/// when a save is due; it never touches flash, so it is host-tested.
namespace alert_history
{

/// @brief Entries kept: two pages of the History list (#203).
inline constexpr size_t kCapacity = 16U;

/// @brief An alert ending again within this long of its entry being raised
/// counts against that entry ("one entry per hour", #203).
inline constexpr uint32_t kFoldWindowMinutes = 60U;

/// @brief Shortest gap between saves after a new entry or a Clear All.
inline constexpr uint32_t kNewEntrySaveDelayMs = 60U * 1000U;

/// @brief Shortest gap between saves when only a folded entry changed.
/// @details A flapping alert updates its entry every time it ends; this
/// keeps it from erasing flash every minute. Each save erases one of the two
/// images, which alternate, so at the few alerts a day expected a sector is
/// erased a few hundred times a year against its 100,000-cycle rating.
inline constexpr uint32_t kFoldSaveDelayMs = 10U * 60U * 1000U;

/// @brief How an alert ended.
enum class Ending : uint8_t
{
    /// Its condition went away.
    Cleared = 0,
    /// The operator accepted it.
    Accepted,
};

/// @brief One past alert. Plain data: stored in flash as it is.
struct Entry
{
    /// Orders entries; higher is newer. Never 0 for a stored entry.
    uint32_t sequence;
    uint32_t raised_minute;
    uint32_t ended_minute;
    /// When the operator last pressed IGNORE on it; 0 if never or unsynced.
    uint32_t ignored_minute;
    /// The AlertCode value.
    uint8_t code;
    AlertSeverity severity;
    Ending ending;
    /// Times it ended within the entry's hour, at least 1.
    uint8_t count;
    /// The operator pressed IGNORE before it ended.
    bool ignored;
    std::array<uint8_t, 3> reserved;
    std::array<char, 32> summary;
    std::array<char, 320> detail;
};

/// @brief The whole history as stored in flash.
struct Image
{
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    /// Increases with every save, so the newer of two copies wins.
    uint32_t save_sequence;
    uint32_t payload_size;
    uint32_t crc32;
    /// Sequence the next entry gets.
    uint32_t next_entry_sequence;
    /// Newest first; only the first entry_count are used.
    std::array<Entry, kCapacity> entries;
    /// Alerts accepted for good: one bit per AlertCode (alert_controller's
    /// acceptance_latches()). After the entries, so a version 1 image, which
    /// ends there, still loads.
    uint32_t latched_codes;
    /// ~latched_codes; the CRC covers only the entries.
    uint32_t latched_codes_check;
};

inline constexpr uint32_t kImageMagic = 0x414C4849U; // "ALHI"
/// @brief 2 added latched_codes; a version 1 image loads with none latched.
inline constexpr uint16_t kImageVersion = 2U;
inline constexpr uint16_t kImageVersionWithoutLatches = 1U;
static_assert(static_cast<size_t>(AlertCode::Count) <= 32U, "latched_codes holds one bit per code");

/// @brief Local minutes now, or 0 before the clock has synced.
uint32_t local_minute(const TimeStatus& time);

/// @brief Empties the history and forgets any pending save (start-up,
/// tests). Does not schedule a save.
void reset();

/// @brief Adds an alert that just ended, or folds it into its entry.
/// @param now_minute local minutes now (0 if unknown).
void record(const ActiveAlert& alert, Ending ending, uint32_t now_minute);

/// @brief Empties the history and schedules a save (CLEAR ALL).
void clear_all();

/// @brief Entries kept.
uint8_t count();

/// @brief True when alert `code` was accepted for good: it stays quiet,
/// across restarts, until alert_controller unlatches it.
bool latched(uint8_t code);

/// @brief Latches or unlatches alert `code`, and schedules a save when that
/// changes anything.
void set_latched(uint8_t code, bool latched);

/// @brief Entry `index`, 0 = newest. Out-of-range gives an empty entry.
const Entry& entry(uint8_t index);

/// @brief The entry with this sequence, or nullptr when it has gone.
const Entry* find(uint32_t sequence);

/// @brief History list pages; always at least one.
uint8_t page_count();

/// @brief Formats a local minute as `HH:MM`, or `--:--` for 0.
void format_time(uint32_t minute, char* out, size_t out_size);

/// @brief Formats a local minute as `24 Nov 06:12`, or `--:--` for 0.
void format_date_time(uint32_t minute, char* out, size_t out_size);

/// @brief Builds the History key's value line, e.g. `[06:12-07:40]`.
/// @param today_minute local minutes now; an entry from another day shows
/// its date instead of its end.
/// @details Kept to 14 characters so it fits a key at 8x12 (LBL-6): the
/// count, when above 1, replaces the end time (`[06:12 x3]`).
void format_key_value(const Entry& entry, uint32_t today_minute, char* out, size_t out_size);

/// @brief Builds the detail page's text: what happened and when, then the
/// alert's own detail.
/// @param today_minute local minutes now; times on another day get a date.
void format_detail(const Entry& entry, uint32_t today_minute, char* out, size_t out_size);

/// @name Persistence (alert_history_store)
/// @{
/// @brief The image to save: header filled in, CRC over the entries.
const Image& image_for_save(uint32_t save_sequence);

/// @brief Takes a loaded image if it is valid. Returns false and keeps an
/// empty history otherwise.
bool restore(const Image& image);

/// @brief True when a valid image's header and CRC check out.
bool image_valid(const Image& image);

/// @brief True when a save is due at `now_ms`.
bool save_due(uint32_t now_ms);

/// @brief Records that the history was saved at `now_ms`.
void mark_saved(uint32_t now_ms);
/// @}

} // namespace alert_history
