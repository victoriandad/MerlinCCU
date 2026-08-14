#include "calendar_controller.h"

#include <cstdio>

#include "calendar_navigation.h"
#include "console_controller_internal.h"

#if __has_include("calendar_identities.h")
#include "calendar_identities.h"
#else
#include "calendar_identities.example.h"
#endif

namespace calendar_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

struct CalendarOwnerDefinition
{
    CalendarOwner owner;
    const char* selection_label;
};

/// @brief Returns the operator-facing label for one calendar owner slot.
/// @details Falls back to a generic "Owner N" label when no identity has
/// been configured for that slot in calendar_identities.h.
const char* owner_identity_label(CalendarOwner owner)
{
    if (owner == CalendarOwner::Combined)
    {
        return "Combined";
    }

    const size_t index = static_cast<size_t>(owner) - 1U;
    if (index < kLocalCalendarIdentities.size() && kLocalCalendarIdentities[index][0] != '\0')
    {
        return kLocalCalendarIdentities[index];
    }

    switch (owner)
    {
    case CalendarOwner::Owner1:
        return "Owner 1";
    case CalendarOwner::Owner2:
        return "Owner 2";
    case CalendarOwner::Owner3:
        return "Owner 3";
    case CalendarOwner::Owner4:
        return "Owner 4";
    case CalendarOwner::Owner5:
        return "Owner 5";
    case CalendarOwner::Owner6:
        return "Owner 6";
    case CalendarOwner::Owner7:
        return "Owner 7";
    case CalendarOwner::Owner8:
        return "Owner 8";
    case CalendarOwner::Combined:
        break;
    }

    return "Owner";
}

/// @brief Returns the static metadata for one selectable calendar owner filter.
const CalendarOwnerDefinition& owner_definition(CalendarOwner owner)
{
    static CalendarOwnerDefinition definitions[] = {
        {CalendarOwner::Combined, "Combined"}, {CalendarOwner::Owner1, "Owner 1"},
        {CalendarOwner::Owner2, "Owner 2"},     {CalendarOwner::Owner3, "Owner 3"},
        {CalendarOwner::Owner4, "Owner 4"},     {CalendarOwner::Owner5, "Owner 5"},
        {CalendarOwner::Owner6, "Owner 6"},     {CalendarOwner::Owner7, "Owner 7"},
        {CalendarOwner::Owner8, "Owner 8"},
    };

    const size_t index = static_cast<size_t>(owner);
    if (index < (sizeof(definitions) / sizeof(definitions[0])))
    {
        definitions[index].selection_label = owner_identity_label(owner);
        return definitions[index];
    }

    return definitions[0];
}

/// @brief Returns the backing event index for one visible Calendar softkey slot.
/// @details Slots are rebuilt from the filtered event list on demand so Home
/// Assistant data can replace the sample rows without duplicate indices.
uint8_t event_index_for_visible_slot(const ConsoleState& console_state, uint8_t visible_slot)
{
    return calendar_navigation::event_index_for_visible_slot(
        console_state.calendar_events, console_state.calendar_event_count,
        console_state.calendar_owner, console_state.calendar_day_offset, visible_slot);
}

} // namespace

const char* owner_selection_text(const ConsoleState& console_state)
{
    return owner_definition(console_state.calendar_owner).selection_label;
}

bool event_matches_filter(const ConsoleState& console_state, const CalendarEvent& event)
{
    return calendar_navigation::event_matches_filter(event, console_state.calendar_owner,
                                                      console_state.calendar_day_offset);
}

/// @details The data portion deliberately carries both time and owner so the
/// Combined view remains useful without needing a wider centre table.
const char* build_event_softkey_label(SoftKeyId key, const CalendarEvent& event)
{
    const char* owner_text = owner_definition(event.owner).selection_label;
    char value[24] = {};
    std::snprintf(value, sizeof(value), "%s %s",
                  event.start_time[0] != '\0' ? event.start_time.data() : "--:--", owner_text);
    return cci::build_selection_softkey_label(key, event.title.data(), value);
}

bool cycle_owner(ConsoleState& console_state)
{
    const CalendarOwner next = calendar_navigation::next_owner(console_state.calendar_owner);
    if (next == console_state.calendar_owner)
    {
        return false;
    }

    console_state.calendar_owner = next;
    return true;
}

bool filters_are_default(const ConsoleState& console_state)
{
    return calendar_navigation::filters_are_default(console_state.calendar_owner,
                                                     console_state.calendar_day_offset);
}

bool reset_filters(ConsoleState& console_state)
{
    if (filters_are_default(console_state))
    {
        return false;
    }

    console_state.calendar_owner = CalendarOwner::Combined;
    console_state.calendar_day_offset = 0;
    return true;
}

/// @details The current UI slice stores days as offsets from today so the same
/// model can be filled by Home Assistant calendar data later.
bool change_day(ConsoleState& console_state, int direction)
{
    if (console_state.active_page != MenuPage::Calendar)
    {
        return false;
    }

    return calendar_navigation::step_day_offset(direction, console_state.calendar_day_offset,
                                                console_state.calendar_day_offset);
}

bool open_detail_from_slot(ConsoleState& console_state, uint8_t visible_slot)
{
    const uint8_t event_index = event_index_for_visible_slot(console_state, visible_slot);
    if (event_index >= console_state.calendar_events.size())
    {
        return false;
    }

    console_state.selected_calendar_event_index = event_index;
    console_state.active_page = MenuPage::CalendarDetail;
    return true;
}

} // namespace calendar_controller
