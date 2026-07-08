#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure Pinter brew-scheduling rules shared between the firmware and
/// host tests (issue #48). Covers dock/fridge capacity accounting and the
/// per-vessel stage-transition rules that gate the CCU's context-sensitive
/// "primary action" softkey. Recipe selection is deliberately on-the-fly (pick
/// a recipe at the moment you start a vessel, from the full catalogue) rather
/// than a pre-planned queue -- there is no brew-pack inventory/reservation
/// concept here, by explicit choice: starting a vessel just asks which recipe
/// you have in hand right now.
namespace pinter_scheduling
{

/// @brief Counts vessels currently occupying a brew dock slot (Brewing only).
uint8_t brew_dock_count(const std::array<PinterStatus, kPinterCount>& pinters);

/// @brief Returns whether a state occupies one of the shared fridge slots.
bool uses_fridge(PinterState state);

/// @brief Counts vessels currently occupying fridge space (Conditioning/Ready).
uint8_t fridge_count(const std::array<PinterStatus, kPinterCount>& pinters);

/// @brief Returns whether this vessel still has a planned cold-crash step pending.
bool has_pending_cold_crash(const PinterStatus& pinter);

/// @brief Returns true when a vessel can start brewing now.
/// @details Cold-crash does not occupy either the dock or the fridge in this
/// model -- a vessel frees its dock slot the moment it leaves Brewing.
bool can_start(uint8_t dock_count);

/// @brief Returns true when a vessel can move into fridge space now.
bool can_enter_fridge(const PinterStatus& pinter, uint8_t fridge_count);

/// @brief Returns whether the context-sensitive primary action is currently allowed.
/// @details This is the single source of truth for why the CCU's one Pinter
/// action button is enabled or blocked; the UI is responsible for explaining
/// *why* when this returns false rather than silently doing nothing.
bool primary_action_enabled(const PinterStatus& pinter, uint8_t dock_count, uint8_t fridge_count);

/// @brief Summarised Pinter workflow bucket counts used on the Home page.
struct SummaryCounts
{
    uint8_t brewing;
    uint8_t conditioning;
    uint8_t ready;
};

/// @brief Counts the user-facing Pinter workflow buckets used on Home.
/// @details Any pre-conditioning hold state (cold crash) remains grouped with
/// brewing for this summary.
SummaryCounts summarize(const std::array<PinterStatus, kPinterCount>& pinters);

/// @brief Advances one non-Idle vessel to its next real-world stage in place.
/// @details Does not handle the Idle -> Brewing transition (see the header
/// note above). Returns false without modifying `pinter` if the vessel is
/// Idle or the action is currently blocked by dock/fridge capacity -- callers
/// should check `primary_action_enabled` first if they need to distinguish
/// "blocked" from "nothing to do" for user-facing messaging.
bool advance_non_idle(PinterStatus& pinter, uint32_t today_epoch_day, uint8_t fridge_count);

/// @brief Clears a vessel back to Idle after a mistaken manual event.
/// @details `default_brew_index` seeds the vessel's next brew selection so it
/// doesn't default back to catalogue entry zero. Returns false (no-op) if the
/// vessel is already Idle with no cold-crash history to clear.
bool reset(PinterStatus& pinter, uint8_t default_brew_index);

/// @brief Returns the epoch day the vessel's current timed stage is aiming to
/// finish (Brewing, ColdCrash, or Conditioning), or 0 when the state has no
/// such target (Idle, Ready, Consumed).
/// @details Stage advancement is a manual event, not automatic on this day --
/// this is advisory information for the UI, not a scheduling deadline the
/// firmware enforces.
uint32_t current_stage_target_day(const PinterStatus& pinter);

/// @brief Returns the current timed stage's planned duration in days (Brewing,
/// ColdCrash, or Conditioning), or 0 when the state has no timed stage.
uint8_t current_stage_planned_days(const PinterStatus& pinter);

/// @brief Nudges the current timed stage's planned duration by `delta` days,
/// pushing its target/ready date later or earlier without altering when the
/// stage actually began.
/// @details A no-op (returns false, `pinter` unchanged) when the state has no
/// timed stage (Idle, Ready, Consumed), or when applying `delta` would take
/// the planned duration below 1 day or above the `uint8_t` storage range.
bool nudge_current_stage_days(PinterStatus& pinter, int delta);

} // namespace pinter_scheduling
