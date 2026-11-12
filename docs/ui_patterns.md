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
  right-edge clipping.

### Info Page Rows

- Intent: show a smaller set of diagnostic fields with stronger separation.
- Use When: focused diagnostics such as keypad debug.
- Source: `draw_info_page_rows()` in `src/display/screens.cpp` (declared in
  `include/display/screens_shared.h`); keypad debug is
  `draw_keypad_debug_page()` in `src/display/settings_screens.cpp`.
- Rules: use shared row rendering rather than bespoke boxes.

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

### Empty State

- Intent: make absence of data explicit without adding synthetic rows.
- Use When: no alerts, shares, events, or selected records exist.
- Source: `draw_centered_text()` in `src/display/screens.cpp`, called from
  each page family (for example `draw_alert_list_page()`). There is no shared
  empty-state block yet.
- Rules: concise uppercase message, centred in the content area (STATE rules
  in `docs/ui_requirements.md`).

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

- Long-value elision or marquee policy.
- Standard graph footer/min/max labelling.
- Shared table column definitions for weather/share-like data.
- Common empty/error/loading state helper.
