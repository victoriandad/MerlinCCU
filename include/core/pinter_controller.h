#pragma once

#include <cstdint>

#include "console_controller.h"
#include "console_model.h"
#include "input.h"
#include "pinter_scheduling.h"

/// @brief Pinter (home-brewing vessel) workflow rules, split out of
/// console_controller.cpp -- issue #44.
/// @details Every function here takes the console's ConsoleState explicitly
/// rather than reaching for a hidden global, matching the pattern already
/// used across the display split (issue #45): callers in console_controller.cpp
/// own the state and pass it in.
namespace pinter_controller
{

using console_controller::PinterBrewTiming;

/// @brief Returns the static scheduling metadata for one Pinter brew pack.
const PinterBrewTiming& brew_timing(uint8_t brew_index);

/// @brief Returns the number of brew packs in the runtime catalogue.
size_t brew_catalogue_count();

/// @brief Returns the recommended start-to-ready duration for one brew pack.
uint8_t recommended_total_days(const PinterBrewTiming& brew);

/// @brief Returns the shortest supported start-to-ready duration for one brew pack.
uint8_t minimum_total_days(const PinterBrewTiming& brew);

/// @brief Returns the number of pages needed to show a Pinter list.
uint8_t list_page_count(size_t item_count);

/// @brief Clamps a Pinter list page index after the catalogue page size changes.
void clamp_list_page(uint8_t& page_index, size_t item_count);

/// @brief Returns a concise state label for Pinter softkeys.
const char* state_selection_text(PinterState state);

/// @brief Counts Pinters currently occupying a brew dock.
uint8_t brew_dock_count(const ConsoleState& console_state);

/// @brief Counts Pinters currently occupying fridge space.
uint8_t fridge_count(const ConsoleState& console_state);

/// @brief Counts the user-facing Pinter workflow buckets used on Home.
pinter_scheduling::SummaryCounts summary_counts(const ConsoleState& console_state);

/// @brief Returns the currently selected Pinter record.
PinterStatus& selected(ConsoleState& console_state);

/// @brief Returns the currently selected Pinter record.
const PinterStatus& selected_const(const ConsoleState& console_state);

/// @brief Returns whether the selected Pinter still has a planned cold crash transition.
bool has_pending_cold_crash(const ConsoleState& console_state);

/// @brief Returns today's local epoch day for Pinter event stamping.
uint32_t current_event_day(const ConsoleState& console_state);

/// @brief Formats one Pinter selector softkey using the current lifecycle state.
const char* build_slot_softkey_label(const ConsoleState& console_state, SoftKeyId key,
                                     const PinterStatus& pinter);

/// @brief Formats the Home-page Pinter summary as brewing/conditioning/ready.
const char* build_home_softkey_label(SoftKeyId key, const ConsoleState& console_state);

/// @brief Formats one catalogue brew item with recommended and minimum totals.
const char* build_catalogue_item_label(SoftKeyId key, uint8_t brew_index);

/// @brief Formats one timing-adjustment label in the pending Pinter start flow.
const char* build_days_label(SoftKeyId key, const char* title, uint8_t days);

/// @brief Returns true when the selected Pinter can be started now.
bool can_start(const ConsoleState& console_state);

/// @brief Returns whether the context-sensitive Pinter action can be applied.
bool primary_action_enabled(const ConsoleState& console_state);

/// @brief Formats the context-sensitive Pinter event action and any block reason.
const char* build_primary_action_label(SoftKeyId key, const ConsoleState& console_state);

/// @brief Updates the on-screen reason the Pinter primary action is blocked, if any.
void update_block_reason(ConsoleState& console_state);

/// @brief Selects one physical Pinter vessel for subsequent event actions.
bool select_slot(ConsoleState& console_state, uint8_t index);

/// @brief Changes the current page of the Pinter recipe catalogue.
bool change_list_page(ConsoleState& console_state, int direction);

/// @brief Loads default recommended timing values for starting the selected Pinter.
bool prepare_start(ConsoleState& console_state, uint8_t brew_index);

/// @brief Handles picking one recipe from the catalogue to start the selected Pinter.
bool select_list_item(ConsoleState& console_state, uint8_t visible_index);

/// @brief Sets pending start timings from one of the catalogue-provided presets.
bool set_pending_timing(ConsoleState& console_state, bool minimum);

/// @brief Adjusts one pending Pinter duration while keeping it in a sensible range.
bool adjust_pending_days(uint8_t& value, int direction, uint8_t minimum, uint8_t maximum);

/// @brief Commits the pending start flow to the selected idle Pinter.
bool confirm_start(ConsoleState& console_state);

/// @brief Applies the normal next real-world event for the selected Pinter.
bool apply_primary_action(ConsoleState& console_state);

/// @brief Clears the selected Pinter back to idle after a mistaken manual event.
bool reset_selected(ConsoleState& console_state);

/// @brief Nudges the selected Pinter's current stage target date by one day.
bool nudge_selected_day(ConsoleState& console_state, int delta);

/// @brief Writes any pending Pinter state to flash, if a save is due.
/// @details Callers must invoke this from a point that is not nested inside
/// network request handling: the underlying flash write disables all
/// interrupts for its duration. See console_controller.h's
/// flush_pending_pinter_save() for the full rationale.
bool flush_pending_save(ConsoleState& console_state);

} // namespace pinter_controller
