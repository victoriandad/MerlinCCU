#pragma once

#include <array>
#include <cstdint>

#include "alert_ordering.h"
#include "console_model.h"
#include "input.h"

/// @brief Alert state synchronisation, split out of console_controller.cpp --
/// issue #44 (staged, following the pinter_controller.cpp precedent).
/// @details Every function here takes the console's ConsoleState explicitly
/// rather than reaching for a hidden global, matching the pattern already
/// used by the display split (issue #45) and pinter_controller.h.
namespace alert_controller
{

/// @brief Rebuilds currently active alerts from live subsystem conditions.
/// @details The debounced alerts (TIME, HOME ASSISTANT, WEATHER, MQTT,
/// KEYPAD) count a failing condition once per `mark_sample_due()`, not per
/// call: sync runs on every state change and twice per keypress, so
/// counting calls made "for 30 seconds" mean a handful of key presses.
void sync(ConsoleState& console_state);

/// @brief Lets the next `sync()` count one sample for the debounced alerts.
/// @details Called once a second by the main loop, just before a sync.
void mark_sample_due();

/// @brief Returns the number of alert list pages required for the current queue.
uint8_t page_count(const ConsoleState& console_state);

/// @brief Sorts active alerts from newest to oldest for list-page mapping.
void build_display_indices(const ConsoleState& console_state,
                           std::array<uint8_t, kActiveAlertCapacity>& out_indices,
                           uint8_t* out_count);

/// @brief Formats one two-line alert softkey label: the summary, then the
/// occurred time with `xN` once the alert has occurred more than once.
const char* build_softkey_label(SoftKeyId key, const ActiveAlert& alert);

/// @brief Summarizes the active queue for lamp/annunciator purposes, without
/// acknowledging it -- see alert_ordering::summarize().
alert_ordering::AnnunciationSummary annunciation_summary(const ConsoleState& console_state);

/// @brief Opens the alert list from the current page when alerts exist.
bool open_list_page(ConsoleState& console_state);

/// @brief Opens one alert-detail page from the currently visible list page slot.
bool open_detail_from_slot(ConsoleState& console_state, uint8_t page_slot);

/// @brief Accepts one alert: hides it until its condition clears and starts
/// its occurrence count again.
/// @details A no-op when `index` is past the active alert count.
void accept_alert(ConsoleState& console_state, uint8_t index);

/// @brief Notes that the operator pressed IGNORE on one alert, and when, so
/// its history entry says so once it ends (#203). It stays active.
/// @details A no-op when `index` is past the active alert count.
void ignore_alert(ConsoleState& console_state, uint8_t index);

/// @brief Opens the History page's detail for the entry in `page_slot`.
bool open_history_detail_from_slot(ConsoleState& console_state, uint8_t page_slot);

/// @brief Keeps the History page index within the pages that exist, e.g.
/// after CLEAR ALL.
void clamp_history_page(ConsoleState& console_state);

/// @brief Removes one alert from the active queue and compacts trailing entries.
void erase_active_alert(ConsoleState& console_state, uint8_t index);

/// @brief Suppresses one alert code so it will not re-trigger until cleared.
/// @details Takes the raw ActiveAlert::code value (an AlertCode, see
/// alert_codes.h).
void suppress_alert_code(uint8_t alert_code_index);

/// @brief True when the alert's condition still holds but the operator has
/// accepted it (#150: reported to Home Assistant for its messages).
bool acknowledged(AlertCode code);

/// @brief Forgets an acceptance so the next activation shows again, e.g. a
/// new Home Assistant message after the last one was accepted.
void rearm(AlertCode code);

/// @brief Counts this unit's own active alerts for its summary to the other
/// units (#210), and finds the most serious (newest of the highest severity).
/// @details Alerts every unit raises alike are left out, so the Units page
/// shows what is wrong with a unit rather than the weather: forecast and
/// official weather warnings, and UNIT OFFLINE and MIXED FIRMWARE themselves.
/// @param top Set to the most serious, or nullptr when there is none.
uint8_t own_alerts(const ConsoleState& console_state, const ActiveAlert** top);

/// @brief The weather alert of `kind` as of the last sync: its id
/// (weather_acks.h), empty while it is not raised.
const weather_acks::Id& weather_ack_id(weather_acks::Kind kind);

/// @brief The alert code a weather acknowledgement `kind` covers.
AlertCode weather_ack_code(weather_acks::Kind kind);

/// @brief Resets alert history, occurrence counts and failure-sample counters,
/// e.g. on device reset.
void reset();

/// @name Softkey maps (page_table.h)
/// @{
void build_list_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys);
void build_detail_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys);
/// @brief History (#203): past alerts newest first, L1-L4 then R1-R4, and
/// CLEAR ALL at L5 with its confirm step.
void build_history_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys);
/// @}

/// @brief History's on_enter hook: first page, no confirm pending.
void enter_history(ConsoleState& console_state);

} // namespace alert_controller
