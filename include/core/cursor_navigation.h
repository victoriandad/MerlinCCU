#pragma once

#include "console_model.h"

/// @brief The one definition of what cursor left and right do on each page
/// (NAV-7, NAV-8, NAV-9).
/// @details Each page that uses the cursor keys moves one position through a
/// bounded range: a list page, a calendar day, or a line of scrolled text.
/// The key handling, the footer arrows and the header's `TITLE n/m` all read
/// that range from here, so an arrow is drawn exactly when its cursor key
/// would change something.
namespace cursor_navigation
{

/// @brief What the cursor keys move through on a page.
enum class CursorUse : uint8_t
{
    None,     ///< The cursor keys do nothing on this page.
    ListPage, ///< Pages of a list; the header shows `TITLE n/m`.
    Step,     ///< Steps a value such as the calendar day.
    Scroll,   ///< Scrolls long text one line at a time.
};

/// @brief The current position and its bounds, inclusive.
struct CursorRange
{
    CursorUse use;
    int position;
    int first;
    int last;
};

/// @brief One page's cursor behaviour, named from its page_table entry: where
/// the cursor is and its bounds, and how a new position is stored.
struct PageCursor
{
    CursorRange (*range)(const ConsoleState& console_state);
    /// Only called with a position `range` allows.
    void (*set_position)(ConsoleState& console_state, int position);
};

/// @brief The cursor behaviours pages use.
extern const PageCursor kSettingsPages;
extern const PageCursor kTimeZonePages;
extern const PageCursor kBrewCataloguePages;
extern const PageCursor kAlertListPages;
extern const PageCursor kHomeControlPages;
extern const PageCursor kCalendarDay;
extern const PageCursor kAlertDetailScroll;

/// @brief What the cursor keys move through on the current page, and where.
CursorRange cursor_range(const ConsoleState& console_state);

/// @brief True when cursor left (`direction` -1) or right (+1) would change
/// the current page's state.
bool can_move(const ConsoleState& console_state, int direction);

/// @brief Moves one position left (-1) or right (+1); false, with nothing
/// changed, when that direction is not available.
bool move(ConsoleState& console_state, int direction);

/// @brief The header's page position for a paged list: true, with the
/// 1-based page and the page count, when the page is a list of more than one
/// page (NAV-9).
bool list_page_position(const ConsoleState& console_state, unsigned& page, unsigned& count);

} // namespace cursor_navigation
