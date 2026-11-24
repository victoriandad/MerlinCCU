#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"
#include "input.h"

/// @brief One builder per softkey role (docs/ui_requirements.md section 3).
/// @details Every softkey on every page is built here, so a key's role, label
/// form, enabled state and inverse state always agree (SK-1, SK-2). A bracketed
/// value is always the current state of what the caption names (SK-4); the
/// role, not the brackets, decides what a press does.
///
/// Builders that take a `SoftKeyId` format `CAPTION\n[Value]` into that key's
/// controller-owned label buffer, so the returned label stays valid until the
/// key is rebuilt. Builders that take a finished `label` use it as given; the
/// feature controllers (Pinter, Calendar, Alerts) format those.
namespace softkey
{

/// @brief List slots in fill order, L1-L4 then R1-R4 (NAV-6).
inline constexpr std::array<SoftKeyId, 8> kListSlots = {
    SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,  SoftKeyId::Left4,
    SoftKeyId::Right1, SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};

/// @brief Places `action` on `key` in a page's softkey map.
inline void set(SoftKeyMap& softkeys, SoftKeyId key, const SoftKeyAction& action)
{
    softkeys[static_cast<size_t>(key)] = action;
}

/// @brief No label; a press does nothing.
SoftKeyAction blank();

/// @brief `CAPTION`: opens the named page.
SoftKeyAction navigate(const char* label, MenuPage page);

/// @brief `CAPTION [Value]`: opens the named page; `value` summarises what
/// that page holds.
SoftKeyAction navigate(SoftKeyId key, const char* caption, const char* value, MenuPage page);

/// @brief `CAPTION [Value]`: opens a page through `route`, which also chooses
/// what that page shows (`slot`), such as which graph.
SoftKeyAction navigate(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route,
                       uint8_t slot);

/// @brief `CAPTION [Value]`: opens `page`, a page of option keys for this value.
SoftKeyAction select(SoftKeyId key, const char* caption, const char* value, MenuPage page);

/// @brief `CAPTION [Value]`: starts entering this value on the current page.
SoftKeyAction select(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route);

/// @brief A select key whose value is being typed in on this page: shown
/// inverse as the field being edited (LBL-5), and inert until the entry is
/// confirmed or cancelled.
SoftKeyAction select_editing(SoftKeyId key, const char* caption, const char* value);

/// @brief `VERB`: does the named thing immediately.
SoftKeyAction action(const char* verb, SoftKeyRoute route);

/// @brief `VERB`: does the named thing to entry `slot` of a list, such as
/// running a Home Assistant scene from the Controls page.
SoftKeyAction action(const char* verb, SoftKeyRoute route, uint8_t slot);

/// @brief `VERB`: leaves a decision state for `page`, such as CANCEL.
SoftKeyAction action(const char* verb, MenuPage page);

/// @brief `VERB [Value]`: does the named thing to the thing `value` names.
SoftKeyAction action(SoftKeyId key, const char* verb, const char* value, SoftKeyRoute route);

/// @brief `CAPTION [Enabled]` or `CAPTION [Disabled]`: switches a two-state
/// setting. Never inverse: the state is in the words (SK-9).
SoftKeyAction toggle(SoftKeyId key, const char* caption, bool on, SoftKeyRoute route);

/// @brief `CAPTION [On]` or `CAPTION [Off]`: switches a device, entry `slot`
/// of a list, such as a light on the Controls page. Devices are on or off,
/// not enabled (SK-9).
SoftKeyAction toggle(SoftKeyId key, const char* caption, const char* state, SoftKeyRoute route,
                     uint8_t slot);

/// @brief `CAPTION [Value]` at L5: steps what the whole page shows to the next
/// value, wrapping round (NAV-4).
SoftKeyAction view(SoftKeyId key, const char* caption, const char* value, SoftKeyRoute route);

/// @brief `CAPTION -` / `CAPTION +`, with or without a value: moves a number
/// one step.
SoftKeyAction step(const char* label, SoftKeyRoute route);

/// @brief `CAPTION [Value]`: shows a value reported from elsewhere; a press
/// does nothing.
SoftKeyAction status(SoftKeyId key, const char* caption, const char* value);

/// @brief A finished status label, such as a blocked action and its reason.
SoftKeyAction status(const char* label);

/// @brief One choice on the page a select key opens; `slot` names the choice
/// for `route`. Inverse when it is the current choice (SK-7).
SoftKeyAction option(const char* label, SoftKeyRoute route, uint8_t slot, bool selected);

/// @brief One entry of a list; `route` opens or selects entry `slot`.
/// `selected` marks the entry the page's other keys act on.
SoftKeyAction item(const char* label, SoftKeyRoute route, uint8_t slot, bool selected = false);

} // namespace softkey
