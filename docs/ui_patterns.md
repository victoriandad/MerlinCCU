# UI Patterns

## Why This Exists

Capture reusable UI patterns found in the current firmware so new pages extend
the existing interface instead of creating new one-off presentation styles.

The rules these patterns implement are numbered in `docs/ui_requirements.md`.
Where a pattern and a rule disagree, the rule wins and this document should be
corrected.

## What Belongs Here

- Named UI patterns with intent, usage rules, and source references.
- Softkey label formats and route conventions.
- Row, table, graph, footer, and empty-state patterns.
- Anti-patterns that have caused inconsistent presentation.

## Pattern Template

### Pattern Name

- Intent:
- Use When:
- Avoid When:
- Source:
- Rules:
- Example:

## Current Patterns

### Standard Page Chrome

- Intent: give every normal page the same title/status frame.
- Use When: drawing any menu page under `screens::draw_menu_screen()`.
- Source: `src/display/screen_banners.cpp`, `src/display/screens.cpp`.
- Rules: call `screen_banners::draw_standard_banners()`, then draw softkeys,
  then draw page-specific content.

### Softkey Selection Label

- Intent: show action and selected value around the bezel.
- Use When: a softkey changes or selects a state.
- Source: `build_selection_softkey_label()` in
  `src/core/console_controller.cpp`; drawn by `draw_softkey_label()` in
  `src/display/screens.cpp`.
- Rules: uppercase caption on the first line, bracketed mixed-case value on the
  second line. Brackets mean "current value" and nothing else (LBL-1 to
  LBL-10).

### Sparse Settings Body

- Intent: avoid duplicating settings values in the centre of the page.
- Use When: the softkeys already show all relevant editable state.
- Source: the "key pages with no body" case list in `draw_menu_screen()`
  (`src/display/screens.cpp`); pages that draw only an edit control or page
  arrows have their own function in `src/display/settings_screens.cpp`.
- Rules: leave the centre clear except for active edit controls or page arrows.

### Compact Detail Rows

- Intent: show many status fields in a scan-friendly layout.
- Use When: status and dense diagnostics pages.
- Source: `draw_compact_detail_rows()` in `src/display/screens.cpp`
  (declared in `include/display/screens_shared.h`), used by
  `draw_status_page()` in `src/display/status_screens.cpp`.
- Rules: uppercase labels, concise mixed-case values, stable row pitch, no
  right-edge clipping. Long values follow the Long Detail Values policy below.

### Info Page Rows

- Intent: show a smaller set of diagnostic fields with stronger separation.
- Use When: focused diagnostics such as keypad debug.
- Source: `draw_info_page_rows()` in `src/display/screens.cpp` (declared in
  `include/display/screens_shared.h`); keypad debug is
  `draw_keypad_debug_page()` in `src/display/settings_screens.cpp`.
- Rules: use shared row rendering rather than bespoke boxes. Long values
  follow the Long Detail Values policy below.

### Long Detail Values

- Intent: never lose a value off the screen edge (SCR-6), and never cut one
  that there is room to show.
- Use When: any detail row whose value can be wider than its column, such as
  calendar text, host names, SSIDs, and share names.
- Source: `draw_detail_value()` in `src/display/screens.cpp`, used by both
  `draw_compact_detail_rows()` and `draw_info_page_rows()`.
- Rules: a value runs from its column to 12 pixels short of the right edge,
  the same margin as the Text block. A wider value wraps under itself (on
  spaces, hard-breaking a long word such as a host name) and later rows move
  down. A block may grow by wrapping only down to the top of the row 5
  softkey cell; once that room is used up, a value is cut at the last
  character that fits and ends with `...`. Earlier rows take the room first.
  A block whose unwrapped rows already reach row 5 gets no extra lines. The
  stored value is never shortened.

### Paged Arrows

- Intent: indicate additional list/settings pages.
- Use When: cursor-left/right can change visible page content.
- Source: `draw_page_navigation_arrows()` in `src/display/screens.cpp`
  (declared in `include/display/screens_shared.h`).
- Rules: use the shared helper so arrow placement remains consistent. Show an
  arrow only for a direction that does something (NAV-8).

### Alert List And Detail

- Intent: separate triage from detailed operator text.
- Use When: presenting active CCU alerts.
- Source: `draw_alert_list_page()` and `draw_alert_detail_page()` in
  `src/display/alert_screens.cpp`; alert softkeys in
  `src/core/console_controller.cpp`.
- Rules: list entries show summary and time; detail pages provide accept,
  ignore, and scroll behaviour.

### Text Block

- Intent: show free text that may be longer than one line or one screen.
- Use When: alert detail, and any future page body that is prose rather
  than label/value rows.
- Source: `screens::wrap_text()` in `src/display/screens.cpp` (declared in
  `include/display/screens_shared.h`); used by `draw_alert_detail_page()` in
  `src/display/alert_screens.cpp`.
- Rules: wrap to the body width on spaces, keep `\n` as a forced break, and
  hard-break a word wider than the line. Scroll with the cursor keys and
  show only the arrows for directions that move (NAV-7, NAV-8). Compute the
  scroll range from the wrapped line count in one function shared by the
  renderer and the key handling (`alert_screens::detail_max_scroll()`).

### Empty State

- Intent: make absence of data explicit without adding synthetic rows.
- Use When: no alerts, shares, events, or selected records exist.
- Source: `screens::draw_empty_state()` in `src/display/screens.cpp`
  (declared in `include/display/screens_shared.h`); used by
  `draw_weather_page()` in `src/display/weather_screens.cpp` and the Local
  Conditions page and graph in `src/display/screens.cpp`. Other pages
  still centre their own message with `draw_centered_text()` and move to the
  block under #117.
- Rules: one uppercase headline at `kEmptyStateHeadlineY` and an optional
  one-line, mixed-case hint at `kEmptyStateHintY`, both centred. The block
  sits between the row 2 and row 3 softkey cells, so it never meets a label.
  The hint says what the operator can do; a not-configured hint names where
  to configure (STATE-2, STATE-5).

### Graph Plot Area

- Intent: give every history graph the same bordered plot and scaling.
- Use When: drawing a value-over-time plot.
- Source: `GraphPlotArea`, `draw_graph_plot_border()`, `graph_plot_x()`, and
  `graph_plot_y()` in `include/display/screens_shared.h`.
- Rules: describe the plot with a `GraphPlotArea` and map points through the
  shared helpers rather than page-local arithmetic.

## Anti-Patterns

- Duplicating the same value in the centre body and on a softkey.
- Creating page-local row geometry where a shared row helper fits.
- Mixing uppercase labels and mixed-case values inconsistently.
- Allowing long values to clip at the right edge.
- Adding new navigation behaviour without updating parent-page/back-step rules.

## Candidates For New Shared Helpers

- Standard graph footer/min/max labelling.
- Shared table column definitions for weather/share-like data.
