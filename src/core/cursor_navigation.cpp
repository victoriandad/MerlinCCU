#include "cursor_navigation.h"

#include <algorithm>

#include "alert_screens.h"

namespace cursor_navigation
{

namespace
{

/// @brief A range of pages `0..page_count-1` at `page_index`.
CursorRange list_pages(uint8_t page_index, uint8_t page_count)
{
    return {CursorUse::ListPage, page_index, 0, static_cast<int>(page_count) - 1};
}

/// @brief Writes a new position into the field the current page's range
/// reads. Only called with a position cursor_range() allows.
void set_position(ConsoleState& console_state, int position)
{
    switch (console_state.active_page)
    {
    case MenuPage::Settings:
        console_state.settings_page_index = static_cast<uint8_t>(position);
        break;
    case MenuPage::TimeZoneSettings:
        console_state.time_zone_page_index = static_cast<uint8_t>(position);
        break;
    case MenuPage::PinterSelectBrew:
        console_state.pinter_catalogue_page_index = static_cast<uint8_t>(position);
        break;
    case MenuPage::AlertList:
        console_state.alert_list_page_index = static_cast<uint8_t>(position);
        break;
    case MenuPage::Calendar:
        console_state.calendar_day_offset = static_cast<int8_t>(position);
        break;
    case MenuPage::AlertDetail:
        console_state.alert_detail_scroll_line = static_cast<uint8_t>(position);
        break;
    default:
        break;
    }
}

} // namespace

CursorRange cursor_range(const ConsoleState& console_state)
{
    switch (console_state.active_page)
    {
    case MenuPage::Settings:
        return list_pages(console_state.settings_page_index, kSettingsPageCount);
    case MenuPage::TimeZoneSettings:
        return list_pages(console_state.time_zone_page_index, kTimeZonePageCount);
    case MenuPage::PinterSelectBrew:
        return list_pages(console_state.pinter_catalogue_page_index, kPinterBrewListPageCount);
    case MenuPage::AlertList:
        return list_pages(console_state.alert_list_page_index,
                          alert_list_page_count(console_state.alert_count));
    case MenuPage::Calendar:
        return {CursorUse::Step, console_state.calendar_day_offset, kCalendarMinDayOffset,
                kCalendarMaxDayOffset};
    case MenuPage::AlertDetail:
        if (console_state.alert_detail_index < console_state.alert_count)
        {
            // The scroll range comes from the renderer's own line wrapping,
            // so the two cannot disagree. The renderer shows a line past the
            // end as the last page, so the position is clamped the same way.
            const ActiveAlert& alert =
                console_state.active_alerts[console_state.alert_detail_index];
            const int max_scroll = alert_screens::detail_max_scroll(alert);
            return {CursorUse::Scroll,
                    std::min<int>(console_state.alert_detail_scroll_line, max_scroll), 0,
                    max_scroll};
        }
        break;
    default:
        break;
    }
    return {CursorUse::None, 0, 0, 0};
}

bool can_move(const ConsoleState& console_state, int direction)
{
    const CursorRange range = cursor_range(console_state);
    const int target = range.position + direction;
    return range.use != CursorUse::None && direction != 0 && target >= range.first &&
           target <= range.last;
}

bool move(ConsoleState& console_state, int direction)
{
    if (!can_move(console_state, direction))
    {
        return false;
    }
    set_position(console_state, cursor_range(console_state).position + direction);
    return true;
}

bool list_page_position(const ConsoleState& console_state, unsigned& page, unsigned& count)
{
    const CursorRange range = cursor_range(console_state);
    if (range.use != CursorUse::ListPage || range.last <= range.first)
    {
        return false;
    }
    page = static_cast<unsigned>(range.position - range.first + 1);
    count = static_cast<unsigned>(range.last - range.first + 1);
    return true;
}

} // namespace cursor_navigation
