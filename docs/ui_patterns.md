# UI Patterns

## Why This Exists

Capture reusable UI patterns found in the current firmware so new pages extend
the existing interface instead of creating new one-off presentation styles.

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
  `src/core/console_controller.cpp`.
- Rules: uppercase caption on the first line, bracketed mixed-case value on the
  second line.

### Sparse Settings Body

- Intent: avoid duplicating settings values in the centre of the page.
- Use When: the softkeys already show all relevant editable state.
- Source: settings page draw functions in `src/display/screens.cpp`.
- Rules: leave the centre clear except for active edit controls or page arrows.

### Compact Detail Rows

- Intent: show many status fields in a scan-friendly layout.
- Use When: status and dense diagnostics pages.
- Source: `draw_compact_detail_rows()` and `draw_status_page()` in
  `src/display/screens.cpp`.
- Rules: uppercase labels, concise mixed-case values, stable row pitch, no
  right-edge clipping.

### Info Page Rows

- Intent: show a smaller set of diagnostic fields with stronger separation.
- Use When: focused diagnostics such as keypad debug.
- Source: `draw_info_page_rows()` in `src/display/screens.cpp`.
- Rules: use shared row rendering rather than bespoke boxes.

### Paged Arrows

- Intent: indicate additional list/settings pages.
- Use When: cursor-left/right can change visible page content.
- Source: `draw_page_navigation_arrows()` in `src/display/screens.cpp`.
- Rules: use the shared helper so arrow placement remains consistent.

### Alert List And Detail

- Intent: separate triage from detailed operator text.
- Use When: presenting active CCU alerts.
- Source: `draw_alert_list_page()`, `draw_alert_detail_page()`, and alert
  softkey builders.
- Rules: list entries show summary and time; detail pages provide accept,
  ignore, and scroll behaviour.

### Empty State

- Intent: make absence of data explicit without adding synthetic rows.
- Use When: no alerts, shares, events, or selected records exist.
- Source: centred text helpers in `src/display/screens.cpp`.
- Rules: concise uppercase message, centred in the content area.

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
