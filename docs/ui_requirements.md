# UI Design Requirements

## Why This Exists

The CCU display is a small monochrome panel surrounded by ten softkeys. Almost
everything the operator can see or do is a short label beside a key. That is a
narrow medium, and it only stays readable if every page uses it the same way.

This document is the rule set for that medium: what a page may put on screen,
what a softkey label may say, and what pressing a key may do. It is written so
that a new page can be designed, reviewed, and tested against numbered rules
instead of by copying whichever existing page looks closest.

- `docs/ui_guidelines.md` explains the intent and tone of the UI.
- `docs/ui_patterns.md` catalogues the shared helpers that implement it.
- This document states the rules. Where they disagree, this document wins and
  the other two should be corrected.

Requirement words: **must** is mandatory, **should** is the default unless a
page records a reason, **may** is permitted.

Some existing pages do not yet meet these rules. Those gaps are tracked as
GitHub issues, not listed here.

## 1. The Screen Model

The logical screen is 252 x 320 pixels, portrait. Every menu page is divided
into the same four zones.

| Zone | Rows (y) | Owner | Content |
| --- | --- | --- | --- |
| Header | 0-33 | Shared chrome | Input mode, page title, connectivity icons, time |
| Body | 34-287 | The page | Rows, table, graph, plot, text, or nothing |
| Softkey cells | Five rows inside the body | The softkey map | One label per key |
| Footer | 288-319 | The page, one use at a time | Paging arrows, stepper, data line, scratchpad |

Softkey cells sit inside the body rows, five on each side:

| Row | Keys | Label centre (y) |
| --- | --- | --- |
| 1 | L1 / R1 | 50 |
| 2 | L2 / R2 | 107 |
| 3 | L3 / R3 | 164 |
| 4 | L4 / R4 | 221 |
| 5 | L5 / R5 | 278 |

A cell is 120 pixels wide, from the bezel edge to just short of the screen
centre line. Left labels are left-aligned at the bezel; right labels are
right-aligned at the bezel. A label never crosses the centre line.

- **SCR-1** Every menu page must be drawn by the shared sequence: clear, header
  chrome, softkey labels, then the page body. A page must not draw its own
  header or its own softkey labels.
- **SCR-2** Zone boundaries, softkey row positions, and cell width must come
  from one shared layout header. Page code must not restate them as local
  numbers.
- **SCR-3** The header belongs to the shared chrome. A page supplies only its
  title text.
- **SCR-4** The footer has exactly one use per page state: paging arrows, a
  stepper (arrow, label, arrow), a data-freshness or provenance line, an entry
  scratchpad, or nothing.
- **SCR-5** Body content must not overlap the cell of any softkey that
  currently has a label. Body content may use the cells of keys that are blank.
- **SCR-6** Nothing may be clipped by the screen edge, and no two elements may
  overlap.

## 2. Page Types

Every page is one of three types. The type decides how the body and the
softkeys share the screen.

| Type | Softkeys | Body | Examples |
| --- | --- | --- | --- |
| Key page | Any of the ten | Empty, apart from the footer and one optional notice line | Home, Settings and its sub-pages, Calendar, Pinter, Shares list, Alert list |
| Body page | Row 5 only (L5, R5) | Rows 1-4 at full width | Status sub-pages, Calendar event, Share detail, Weather, ADS-B, graphs, Alert detail |
| Diagnostic page | Row 5 only | Free-form test or bring-up content | Keypad debug, Greyscale test |

- **PAGE-1** Each page must declare its type, and its softkey map and body must
  obey that type.
- **PAGE-2** A key page must not repeat in the body any value that a softkey
  label already shows.
- **PAGE-3** A key page may show one notice line in the body, centred, in a gap
  between softkey rows, for information the operator must not miss (for
  example why an action is blocked). It must not be used for routine status.
- **PAGE-4** A body page must keep softkeys to row 5. If a body page needs more
  than two keys, it should be split into a key page that leads to body pages.
- **PAGE-5** A page that changes type with its state (for example a page that
  is a body page when it has data and a key page when unconfigured) must obey
  the rules of the type it is currently showing.

## 3. What A Softkey Can Be

The operator reads a key by its shape, and there are only two shapes.

- **A label on its own** means pressing the key does something, usually
  opening another page.
- **A label with a value in brackets** means the key is showing data. Pressing
  it either opens a page listing the choices for that data, or does nothing
  because the value is a status reported from elsewhere.

A softkey has exactly one role at a time. The role fixes both how the label is
written and what a press does. There are no other roles; a page that needs
something else needs a change to this document first.

| Role | Label form | A press |
| --- | --- | --- |
| Navigate | `CAPTION` | Opens the named page |
| Action | `VERB` | Does the named thing immediately |
| Select | `CAPTION` + `[Value]` | Opens a page of option keys listing the choices for this value |
| Status | `CAPTION` + `[Value]` | Nothing; the value is reported from elsewhere |
| Option | `OPTION NAME`, shown inverse when selected | Chooses this option; used on the page a select key opens |
| Item | `Summary` + `[Qualifier]` | Opens one entry of a list |
| Blank | No label | Nothing |

- **SK-1** Each softkey must have exactly one role from the table above.
- **SK-2** Each role must be built by its own shared label helper, so that the
  label form, the enabled state, and the inverse state cannot be combined in
  ways the table does not allow.
- **SK-3** A key with a label and no value must act when pressed, and the
  result must be visible in the next frame.
- **SK-4** A key with a bracketed value must be either a select key or a
  status key. It must not change the value in place.
- **SK-5** A key that cannot act at the moment must be blank or a status key.
  A bare label must never be shown on a key that will not act.
- **SK-6** Destructive or hard-to-undo actions must be action keys on their own
  page or state, never the same key as a navigation.
- **SK-7** The page a select key opens must show the choices as option keys,
  with exactly one shown as selected, and choosing one must take effect
  immediately.
- **SK-8** Two keys on one page must not do the same thing.

### To confirm: keys that change a value in place

SK-4 follows the rule as stated in review. Several existing keys change their
value on the key itself instead of opening a page of choices: the two-state
`ENABLED [Enabled]` keys, the stepping `PERIOD`, `VIEW`, and `PERSON` keys, and
the Pinter `BREW -` / `BREW +` day keys. Under SK-4 each of these becomes a
select key with its own option page. If in-place change should remain allowed
for some of them, it needs adding here as a named role.

## 4. How A Label Is Written

- **LBL-1** A label has at most two lines: a caption line and a value line.
  Item keys on rows 1-4 may opt in to a third line. Row 5 keys must never use
  three lines, because the third line would enter the footer.
- **LBL-2** The caption names the thing or the action. It must be uppercase.
- **LBL-3** Square brackets mean one thing only: the text inside is the current
  value or state of whatever the caption names. Brackets must not be used for
  decoration, hints, or instructions.
- **LBL-4** Values inside brackets must be mixed case, in operator-facing words
  (`Enabled`, `Open-Meteo`, `No net`), not protocol or code terms.
- **LBL-5** Inverse video means one thing only: this key is the selected option
  of a set, or the field currently being edited.
- **LBL-6** Every line must fit the 120-pixel cell at the font in use. The code
  that builds a label is responsible for producing text that fits, by choosing
  a compact form. Text that the renderer has to cut off is a defect.
- **LBL-7** A caption with a value line must fit on one line. A caption with no
  value line may use two lines, split at a deliberate break.
- **LBL-8** Captions should be one or two words. Where a word must be
  shortened, the same abbreviation must be used everywhere that word appears.
- **LBL-9** Secrets must never be shown. A secret's value line shows only
  whether it is set.
- **LBL-10** A missing value is shown as `-`, never as an empty bracket pair.

## 5. Where Things Go

- **NAV-1** `BACK STEP` must return to the page's parent. Every page has
  exactly one parent.
- **NAV-2** R5 must be `HOME` on every page except Home itself, unless the page
  is in a decision or entry state (NAV-3).
- **NAV-3** When a page is asking for a decision or accepting typed entry, R5
  must be the confirming action (`ACCEPT`, `ENTER`) and L5 the declining one
  (`IGNORE`, `CANCEL`). No other key may confirm.
- **NAV-4** A page's view modifier (the key that changes what the whole
  page shows, such as `PERIOD`, `VIEW`, or `PERSON`) must be at L5.
- **NAV-5** No softkey may duplicate `BACK STEP`.
- **NAV-6** Lists of items must fill L1 to L4 and then R1 to R4, eight to a
  page, leaving row 5 for NAV-2 to NAV-4. The Alert list is the one exception:
  it has no view modifier, so it also uses L5 and shows nine to a page.
- **NAV-6a** A list must never be capped below the number of entries that can
  exist; it pages as far as it needs to. Where entries are of known types
  (such as alerts, one per alert type), the list must grow automatically when
  a type is added, and a repeat of the same entry must raise a count on the
  existing key, not add another key.
- **NAV-7** Cursor left and right must be the only way to change list page,
  step a date or period, or scroll long text. Softkeys must not be spent on
  previous/next.
- **NAV-8** Whenever cursor left or right would do something, the footer must
  show the matching arrow using the shared arrow helper, and must not show an
  arrow for a direction that does nothing.
- **NAV-9** A paged list must show its position as `TITLE n/m` in the header
  whenever there is more than one page.
- **NAV-10** Hard keys with a global meaning (`ALERT`, `TEST`, `BRT`, `DIM`,
  `LTRS`) must behave identically on every page.
- **NAV-11** The same destination should be at the same key position on every
  page that offers it.

## 6. Type And Case

Fonts are chosen by what the text is for, not page by page.

| Category | Default font | Case |
| --- | --- | --- |
| Header title | Title 8x12 | Uppercase |
| Softkey caption | 8x12 | Uppercase |
| Softkey value | 8x12 | Mixed |
| Body row label | 5x7 | Uppercase |
| Body value | 5x7 | Mixed |
| Table column header | 5x7 | Mixed case allowed |
| Sub-header | 5x7 | Uppercase |
| Empty-state headline | 8x12 | Uppercase |
| Footer text | 5x7 | Uppercase label, mixed value |

- **TXT-1** Every piece of text must belong to one category from the table,
  and takes its font from that category. Page code must not pick a font face
  directly.
- **TXT-2** The font for each category must be changeable in one place, and
  selectable at run time from a Settings page, so that sizes can be tried on
  the panel without rebuilding.
- **TXT-3** Everything that depends on a font (line spacing, bracket size,
  wrapping width, lines per label, row pitch) must be derived from that
  font's measurements, so any category can take any available font.
- **TXT-4** Row labels, captions, headings, and empty-state messages must be
  uppercase. Live values must be mixed case.
- **TXT-5** Table column headers may be mixed case.
- **TXT-6** Units must be written the same way everywhere a quantity appears.

## 7. Body Content

A body is built from a small set of shared blocks.

| Block | Use for |
| --- | --- |
| Compact detail rows | Many `LABEL: value` pairs |
| Info rows | A few fields with stronger separation |
| Table | Repeated records in columns |
| Graph | A history plot with axis, period, and min/max labels |
| Text | Scrollable multi-line text |
| Plot | A spatial view |
| Empty state | No data to show |

- **BODY-1** A body must be built from the shared blocks. A new block must be
  added to the shared code and to `docs/ui_patterns.md`, not written inside one
  page.
- **BODY-2** Body content must start at the shared content-top line and use one
  of the shared row pitches.
- **BODY-3** Tables must share one header position, divider, first-row
  position, row pitch, and left margin. A page defines only its columns.
- **BODY-4** Graphs must share one plot position and labelling scheme. Graphs
  of different sizes are permitted only where a page also shows rows beneath.
- **BODY-5** Values must be formatted by shared formatters, one per quantity,
  so the same quantity reads the same on every page.
- **BODY-6** Row counts per page and paging arithmetic must each be defined
  once and shared between the code that builds softkeys, the code that draws
  the page, and the code that titles it.

## 8. No Data, Waiting, Stale, Failed

- **STATE-1** Every page that shows data must define what it shows when there
  is no data, when data is awaited, when data is stale, and when the source
  has failed or is not configured.
- **STATE-2** Empty and waiting states must use the shared empty-state block:
  one uppercase headline at a fixed position, with one optional hint line
  beneath it saying what the operator can do.
- **STATE-3** Pages backed by fetched data must show freshness (`LIVE`,
  `STALE 12m`, `NO DATA`) through the shared freshness helper, in the footer or
  a `DATA` row.
- **STATE-4** The age at which data counts as stale must be defined by the
  subsystem that fetches it, not by the page that draws it.
- **STATE-5** A not-configured state must say where to configure it.
- **STATE-6** A user-visible failure mode must have an alert, as required by
  `docs/alerts.md`.

## 9. Building And Checking A Page

- **IMPL-1** A new page must be checked against this document in its pull
  request, using the page checklist in the pull request template.
- **IMPL-2** Every page must have a golden-image host test for each of its
  states (normal, empty, and any paged or editing state), rendered with its
  real softkey map.
- **IMPL-3** Host tests must check, for every page, that no softkey label was
  cut off (LBL-6) and that the body does not overlap a labelled softkey cell
  (SCR-5).
- **IMPL-4** Page rendering code must stay free of hardware dependencies so it
  remains host-testable.
- **IMPL-5** Changes to shared layout, fonts, or label helpers must regenerate
  all golden images in the same pull request.

## New Page Checklist

1. Choose the page type (section 2).
2. Assign a role to each softkey it uses (section 3); leave the rest blank.
3. Confirm row 5 follows NAV-2 to NAV-4.
4. Write each label and confirm it fits (section 4).
5. Build the body from shared blocks (section 7).
6. Define the no-data, waiting, stale, and failed states (section 8).
7. Add golden images for every state (section 9).
8. Check on `/preview`, and on the panel when layout changed.
