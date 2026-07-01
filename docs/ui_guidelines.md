# UI Guidelines

## Why This Exists

The MerlinCCU UI is a constrained embedded operator interface. This document
captures the presentation rules that already exist in the firmware so future
pages stay consistent with the current panel behaviour.

## Scope

This document records only patterns that are already implemented or strongly
implied by the current UI code:

- global UI design principles
- navigation rules
- layout standards
- alert presentation standards
- status display standards
- configuration screen standards
- diagnostic screen standards

## UI Design Principles

- Treat the device as an operator panel, not a general-purpose web UI.
- Optimise for quick scanning, predictable placement, and low interaction cost.
- Prefer stable locations for recurring information over page-local invention.
- Keep the centre of the screen for page content only when that content needs
  to be visible.
- Let the softkeys carry state when the screen body would otherwise repeat the
  same value.
- Use compact operator-facing words such as `Up`, `Disabled`, `Unconfig`,
  `Bad auth`, and `No net` instead of raw subsystem jargon on normal pages.
- Keep the UI visually calm and mechanical rather than decorative.

## Navigation Rules

- The ten physical softkeys are the primary navigation surface.
- Softkey labels describe the current action or selected value.
- Selected values are shown in brackets on softkeys when a choice is active,
  for example `PERIOD [Today]`.
- `BACK STEP` returns to the logical parent page.
- `R5=HOME` remains available where navigation would otherwise become deep.
- Cursor arrows are used for paging lists, dates, alert pages, and graph
  periods when those screens have more content than fits on one view.
- Page routes should feel stateful and reversible rather than modal.
- Hard keys such as `ALERT`, `TEST`, `BRT`, `DIM`, and `LTRS` can act as
  global shortcuts when their behaviour is active on every page.

## Layout Standards

- Use `screen_banners::draw_standard_banners()` for normal menu pages.
- Reserve the top banner for the title, LTRS mode, connectivity icons, and
  time/status indicators.
- Draw the shared shell first, then the softkeys, then the page-specific body.
- Keep labels and headings uppercase.
- Keep live values in mixed case when readability improves.
- Keep text within the logical framebuffer and clear of softkey labels,
  footer text, and page arrows.
- Prefer a sparse centre on settings pages when the softkeys already carry the
  editable state.
- Use the available content area directly rather than framing everything in
  extra boxes or borders.
- Preserve unclipped right-edge text.
- Use shared row helpers for status and information pages instead of page-local
  ad hoc geometry.

## Alert Presentation Standards

- Alert workflow is list first, detail second.
- The alert list is newest-first.
- The alert list uses compact rows with summary and occurrence time.
- A blank alert list explicitly shows `NO ACTIVE ALERTS`.
- Alert detail pages show the selected alert text with line-based scrolling when
  the detail exceeds the visible area.
- `R5=ACCEPT` removes the selected alert and returns to the list.
- `L5=IGNORE` returns to the list without removing the alert.
- Cursor arrows scroll additional detail text when present.
- Alert summaries should be short enough to fit list and softkey constraints.
- Alert detail text should explain the impact and next likely operator action.
- Alert presentation should remain concise and operational rather than verbose.

## Status Display Standards

- Status pages should summarise current state without making the operator infer
  meaning from logs.
- Status values use concise mixed-case or abbreviated labels when space is
  tight.
- Boolean or connection states are shown with operator-facing terms rather than
  technical protocol names.
- Compact detail rows are the standard format for dense status pages.
- Info pages use stronger separation than status pages when the screen is
  showing a smaller number of diagnostic fields.
- Fixed row pitch and consistent alignment matter more than decorative grouping.
- The page should remain readable even when some values are unavailable, using
  placeholder text such as `-`.

## Configuration Screen Standards

- Configuration pages favour softkey selection over large on-screen forms.
- Many settings pages intentionally leave the centre blank because the current
  value already appears on the softkeys.
- Bracketed softkey values represent the current selection or stored value.
- Changes should be reflected immediately in the visible softkey labels.
- Settings navigation is paged when the category set does not fit on one view.
- Top-level settings pages act as routing surfaces rather than information
  dumps.
- Configuration values should remain short enough to fit on the bezel labels.
- When the current value is long, prefer a compact operator-facing form over
  a raw protocol string.

## Diagnostic Screen Standards

- Diagnostics should remain scan-friendly and use the same visual language as
  the rest of the UI.
- The keypad debug page uses compact rows rather than a boxed instrument panel.
- Diagnostics should show the minimum data required to explain the observed
  state.
- Hardware-facing values such as masks, probe hits, counts, and decoded key
  names are legitimate diagnostic content.
- Diagnostics can expose terse state values such as `ERR`, `PASS`, `FAIL`, or
  `MULTI` when those values help with bring-up.
- If a diagnostic page is paged, cursor arrows should communicate that
  additional pages exist.
- Placeholder diagnostic pages are acceptable when the route exists before the
  feature is fully implemented.

## Cross-Cutting Rules

- Uppercase headings and labels, mixed-case values.
- No clipped text.
- No duplicated state in the body when the softkeys already communicate it.
- Use shared helpers for recurring layout fragments.
- Keep alerts, settings, status, and diagnostics visually related rather than
  designing each page from scratch.

## Validation Expectations

- Check UI changes in `/preview`.
- Check hardware-specific differences on the physical panel when relevant.
- Update framebuffer baselines when a stable page layout intentionally changes.

