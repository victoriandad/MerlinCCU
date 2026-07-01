# UI Guidelines

## Why This Exists

The MerlinCCU UI is a constrained embedded operator interface. This document
keeps look-and-feel, navigation behaviour, layout conventions, status
presentation, and operator workflow out of transient chat prompts.

## What Belongs Here

- Global UI principles and vocabulary.
- Navigation and softkey behaviour.
- Page layout rules for the monochrome framebuffer.
- Status, diagnostics, warnings, and alert-presentation rules.
- Validation expectations for browser preview and physical-panel checks.

## Product Character

- Treat the CCU as a compact aircraft-style control panel: dense, legible,
  predictable, and fast to scan.
- Optimise for operator confidence on a small monochrome display, not decorative
  presentation.
- Prefer stable locations for recurring information over per-page invention.

## Global Presentation Rules

- Use uppercase for headings, fixed labels, and softkey captions.
- Use mixed case for live values where it improves readability.
- Keep text within the logical framebuffer and clear of softkey labels,
  footers, and page arrows.
- Use concise operator-facing words such as `Up`, `Disabled`, `Unconfig`,
  `Bad auth`, and `No net`; avoid raw subsystem jargon unless on diagnostics
  pages.
- Prefer shared helpers for common row, graph, footer, and navigation elements.

## Navigation Rules

- The ten physical softkeys are the primary navigation and selection surface.
- Bracketed softkey values show current selections, for example
  `PERIOD [Today]`.
- The centre of settings pages may remain blank when the surrounding softkeys
  already carry the editable state.
- `BACK STEP` moves to the logical parent page. `R5=HOME` should remain
  available where workflows can otherwise become deep.
- Cursor arrows page through lists, dates, alert pages, and graph periods when
  appropriate.

## Page Layout Rules

- Use `screen_banners::draw_standard_banners()` for normal menu pages.
- Reserve the top banner for title, LTRS mode, connectivity icons, and time.
- Use compact detail rows for dense status pages and info rows for focused
  diagnostics.
- Prefer uncluttered central pages with softkey state over duplicating the same
  data in the body and the bezel labels.
- Graphs should use the available content area cleanly, with direct min/max or
  axis labels and no unnecessary bounding boxes.

## Status and Alerts

- Status pages should summarise current state without forcing the operator to
  infer from logs.
- Alerts should use concise summaries and detail text that explains the impact
  and next likely operator action.
- Alert workflow is list first, detail second: newest-first list entries,
  readable occurrence time, accept/ignore actions, and scrollable detail text.

## Validation

- Check UI changes in `/preview` and, where hardware-specific rendering may
  differ, on the physical panel.
- Use `docs/display-test-checklist.md` for regression and capture workflow.
- Add or update framebuffer baselines when a stable page layout intentionally
  changes.

## Open Guidelines To Define

- Desired Home-page information hierarchy.
- Final weather iconography and warning presentation.
- Long-value truncation or scrolling policy.
- Exact operator workflow for calendar and share configuration.
