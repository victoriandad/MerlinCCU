#include "cursor_navigation.h"

#include <algorithm>

#include "alert_history.h"
#include "alert_screens.h"
#include "page_table.h"

namespace cursor_navigation
{

namespace
{

/// @brief A range of pages `0..page_count-1` at `page_index`.
CursorRange list_pages(uint8_t page_index, uint8_t page_count)
{
    return {CursorUse::ListPage, page_index, 0, static_cast<int>(page_count) - 1};
}

CursorRange alert_detail_range(const ConsoleState& console_state)
{
    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        return {CursorUse::None, 0, 0, 0};
    }
    // The scroll range comes from the renderer's own line wrapping, so the
    // two cannot disagree. The renderer shows a line past the end as the last
    // page, so the position is clamped the same way.
    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    const int max_scroll = alert_screens::detail_max_scroll(alert);
    return {CursorUse::Scroll, std::min<int>(console_state.alert_detail_scroll_line, max_scroll), 0,
            max_scroll};
}

} // namespace

const PageCursor kSettingsPages = {
    [](const ConsoleState& s) { return list_pages(s.settings_page_index, kSettingsPageCount); },
    [](ConsoleState& s, int position) { s.settings_page_index = static_cast<uint8_t>(position); }};

const PageCursor kTimeZonePages = {
    [](const ConsoleState& s) { return list_pages(s.time_zone_page_index, kTimeZonePageCount); },
    [](ConsoleState& s, int position) { s.time_zone_page_index = static_cast<uint8_t>(position); }};

const PageCursor kBrewCataloguePages = {
    [](const ConsoleState& s)
    { return list_pages(s.pinter_catalogue_page_index, kPinterBrewListPageCount); },
    [](ConsoleState& s, int position)
    { s.pinter_catalogue_page_index = static_cast<uint8_t>(position); }};

const PageCursor kAlertListPages = {
    [](const ConsoleState& s)
    { return list_pages(s.alert_list_page_index, alert_list_page_count(s.alert_count)); },
    [](ConsoleState& s, int position) { s.alert_list_page_index = static_cast<uint8_t>(position); }};

const PageCursor kAlertHistoryPages = {
    [](const ConsoleState& s)
    {
        // The CLEAR ALL question holds the page still (NAV-3).
        return s.alert_history_clear_confirming
                   ? CursorRange{CursorUse::None, 0, 0, 0}
                   : list_pages(s.alert_history_page_index, alert_history::page_count());
    },
    [](ConsoleState& s, int position)
    { s.alert_history_page_index = static_cast<uint8_t>(position); }};

const PageCursor kAlertHistoryDetailScroll = {
    [](const ConsoleState& s)
    {
        const int max_scroll = alert_screens::history_detail_max_scroll(s);
        return CursorRange{CursorUse::Scroll,
                           std::min<int>(s.alert_history_scroll_line, max_scroll), 0, max_scroll};
    },
    [](ConsoleState& s, int position)
    { s.alert_history_scroll_line = static_cast<uint8_t>(position); }};

const PageCursor kHomeControlPages = {
    [](const ConsoleState& s)
    {
        return list_pages(s.home_controls.page_index, home_controls::page_count(s.home_controls));
    },
    [](ConsoleState& s, int position) { s.home_controls.page_index = static_cast<uint8_t>(position); }};

const PageCursor kFontPages = {
    [](const ConsoleState& s) { return list_pages(s.font_page_index, kFontPageCount); },
    [](ConsoleState& s, int position) { s.font_page_index = static_cast<uint8_t>(position); }};

const PageCursor kCalendarDay = {
    [](const ConsoleState& s) {
        return CursorRange{CursorUse::Step, s.calendar_day_offset, kCalendarMinDayOffset,
                           kCalendarMaxDayOffset};
    },
    [](ConsoleState& s, int position) { s.calendar_day_offset = static_cast<int8_t>(position); }};

const PageCursor kAlertDetailScroll = {
    alert_detail_range,
    [](ConsoleState& s, int position)
    { s.alert_detail_scroll_line = static_cast<uint8_t>(position); }};

CursorRange cursor_range(const ConsoleState& console_state)
{
    const PageCursor* cursor = page_table::describe(console_state.active_page).cursor;
    return cursor != nullptr ? cursor->range(console_state) : CursorRange{CursorUse::None, 0, 0, 0};
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
    page_table::describe(console_state.active_page)
        .cursor->set_position(console_state, cursor_range(console_state).position + direction);
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
