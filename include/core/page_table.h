#pragma once

#include <cstdint>

#include "console_model.h"
#include "cursor_navigation.h"
#include "text_styles.h"

/// @brief One table entry per menu page: everything the controller, the
/// renderer and the main loop need to know about a page (issue #120).
/// @details Adding a page is one entry here, one softkey builder in its
/// family's controller, and one body renderer in its family's screens file.
/// The title, parent, label font, dispatch, cursor behaviour and redraw
/// policy are all read from the entry, so nothing else lists pages.
namespace page_table
{

/// @brief How the body and the softkeys share the screen
/// (docs/ui_requirements.md section 2, PAGE-1).
enum class PageType : uint8_t
{
    Key,        ///< Any of the ten softkeys; body empty apart from footer and notice.
    Body,       ///< Softkeys on row 5 only; body uses rows 1-4.
    Diagnostic, ///< Softkeys on row 5 only; free-form test content.
};

/// @brief What makes the main loop redraw a page whose state has not changed.
enum class Refresh : uint8_t
{
    OnChange,  ///< Only when the console state changes.
    DataAge,   ///< Also periodically, so a data-age line advances.
    Telemetry, ///< Also when sampled load, heap, stack or scanout figures change.
};

/// @brief Which share prices the main loop fetches while the page is shown.
enum class ShareFetch : uint8_t
{
    None,
    Today,          ///< The day's prices, for the watch list.
    SelectedPeriod, ///< The history period chosen on the page.
};

/// @brief Fills a page's softkey map; keys left untouched stay blank. May
/// update page state the keys depend on, such as a block reason.
using SoftkeyBuilder = void (*)(ConsoleState& console_state, SoftKeyMap& softkeys);

/// @brief Draws a page's body below the shared header and softkey labels.
using PageRenderer = void (*)(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

/// @brief Prepares page state when a softkey opens the page.
using EnterHook = void (*)(ConsoleState& console_state);

/// @brief Everything one menu page declares about itself.
struct PageDescriptor
{
    MenuPage page;
    /// Header title; paged lists add `n/m` (NAV-9).
    const char* title;
    /// Where BACK STEP goes. The Alert list returns to whichever page opened it.
    MenuPage parent;
    PageType type;
    /// Softkey label style: Softkey, or SoftkeyDense for selection-heavy pages.
    text_styles::TextStyle label_style;
    /// Lines one softkey label may wrap onto.
    uint8_t label_max_lines;
    /// Builds the page's own keys; nullptr for a page with only R5 HOME.
    SoftkeyBuilder build_softkeys;
    /// Draws the body; nullptr for a key page whose values are all on keys.
    PageRenderer render;
    /// Runs when a softkey opens the page (not on BACK STEP); may be nullptr.
    EnterHook on_enter;
    /// What the cursor keys move through; nullptr when they do nothing.
    const cursor_navigation::PageCursor* cursor;
    Refresh refresh;
    ShareFetch share_fetch;
};

/// @brief The entry for `page`. `page` must be a real page, not Count.
const PageDescriptor& describe(MenuPage page);

/// @brief The page BACK STEP leads to from the current page.
MenuPage parent_page(const ConsoleState& console_state);

/// @brief Opens `page` as a softkey does: leaves any open entry, runs the
/// page's on_enter hook, and makes it the active page. False for a value
/// that is not a page.
bool open(ConsoleState& console_state, MenuPage page);

} // namespace page_table
