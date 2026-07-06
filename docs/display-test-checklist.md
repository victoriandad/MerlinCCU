
# Display Test Checklist

This branch uses a repeatable display test flow so UI changes can be checked
quickly without relying only on photographs of the physical panel.

## Test Setup

1. Flash firmware from the VS Code Pico extension.
2. Open `http://merlinccu/preview`.
3. Keep serial logs open for network/weather status diagnostics.

## Global UI Consistency Rules

- Labels, headings, and softkey captions: uppercase.
- Data values: mixed case.
- No clipped text at the right edge.
- No overlap between rows, footer text, and softkeys.

## Page Checks

## Home Assistant Status Page

- `HA REST`, `HA REST ST`, `HA REST HOST`, `WX FETCH`, `WX HOST`, `HA MQTT`
  labels are uppercase.
- Values use mixed case consistently (for example `Disabled`, `Error`, `Up`).
- Hostnames and IP values are fully visible.

## Status Resources Page

- `PROGRAM FLASH` and `STATIC RAM` bars are visible and readable.
- Flash usage shows firmware image use against flash available after reserved
  config sectors.
- RAM usage still shows fixed static display/model allocation.
- `LOOP LOAD` shows foreground main-loop active time versus intentional sleep
  over a recent sample; it is not whole-chip CPU usage.
- Unknown live heap telemetry remains an explicit placeholder, `HEAP LIVE -`.

## Weather Page

- `PERIOD` softkey cycles `Hourly` -> `Next 24 Hours` -> `Next 7 Days` and wraps
  back to `Hourly`.
- Forecast table columns align (`Time`, `Temp`, `Wind mph`, `Conditions`).
- `Next 24 Hours` period represents the next 24 hours using the same single-line
  table columns, including `Conditions`.
- `Next 7 Days` period shows provider daily rows (typically up to 7 entries) with
  labels (`Today`, `Tmrw`, then date labels like `DD/MM`).
- Times are local-time oriented and progress sensibly from the current period.
- Sunrise and sunset are both present when provider data is available.
- Source footer is visible and readable.

## Pinter Page

Recipe selection is on-the-fly: there is no pre-planned brew queue or pack
inventory. Starting an idle vessel goes straight to picking a recipe from the
full 35-item catalogue, right when you actually have a pack in hand.

- Home `L4` shows the Pinter summary as `[nB, nC, nR]` (brewing, conditioning,
  ready) and opens the Pinter scheduler page.
- `L1` to `L4` select `P1`, `P3 A`, `P3 B`, and `P3 C`; the selected Pinter is
  inverted on the softkey.
- `R1` is context-sensitive:
  - Selected Pinter `Idle`: labelled `START`, opens the recipe catalogue
    (`SELECT BREW` page). Shows `START\n[NO DOCK]` and is disabled when the
    brew dock is full (3 vessels already `Brewing`).
  - Selected Pinter not idle: applies the normal next event (move to cold
    crash or fridge, ready, drink, or clean), depending on state.
- Pinter centre data is intentionally blank, except a block-reason message
  (for example dock/fridge full) appears when `R1` is disabled.
- Moving to the fridge is blocked once two Pinters are conditioning or ready;
  cold crash is exempt from the fridge-capacity check.
- `R3=RESET` clears the selected Pinter back to idle after a mistaken event.
- Recipe catalogue (`SELECT BREW`) page:
  - Softkeys list up to 8 recipes per page (35 total), each showing the name
    plus recommended/minimum total days, for example `Public House / R12 M7`.
  - `<`/`>` cursor keys page through the catalogue; page arrows only show
    when there is a previous/next page.
  - `R4=PINTER` returns to the main Pinter page; `R5=HOME` returns to Home.
  - Picking a recipe opens the `BREW TIMING` page.
- `BREW TIMING` page:
  - `L1=MINIMUM` / `L2=RECOMM` snap brewing/cold-crash/conditioning days to
    the recipe's minimum or recommended totals.
  - `L3`/`R3` adjust brew days down/up; `L4`/`R4` adjust conditioning days
    down/up; `L5`/`R5` adjust cold-crash days down/up (0 by default, since
    cold crash is optional).
  - `R1=START` confirms and starts the vessel brewing now; disabled if the
    dock filled up while adjusting timing.
  - `R2=BREW` returns to the recipe catalogue to pick a different recipe.
- `http://merlinccu/pinter` shows one row per Pinter, real date ticks, and a
  vertical today marker, plus summary tiles for brewing/conditioning/ready
  counts. Starting, moving to fridge, and marking ready stamp event dates
  from the CCU clock.

## Shares Pages

- Home `L2` opens the shares watchlist.
- Shares list shows `BAE SYSTEMS` with a bracketed price value on `L1` and no
  duplicated share data in the display body.
- Pressing the BAE Systems softkey opens the share detail page.
- Share detail page shows a full-width graph in the upper display region with
  no graph bounding box and no overlap with softkey labels.
- `MIN` (left) and `MAX` (right) graph values are shown directly beneath the
  graph and remain readable across all periods.
- `PERIOD` softkey at `L5` cycles `Today` -> `Week` -> `Month` -> `Year` ->
  `All-time` and wraps back to `Today`.
- `R5=HOME` remains available from the shares pages.
- Until the Home Assistant/local share feed is implemented, share detail shows
  explicit demo data (`DATA Demo`, `PRICE Demo`, `CHANGE No live`) and period
  cycling uses placeholder history only.
- Future live share data should come from a bounded local feed, not direct
  provider scraping on the Pico, and must not block navigation.

## Settings Pages

- Softkey top-line captions are uppercase.
- Bracketed selected values remain mixed case.
- Selection changes update visible values immediately.

## Alert Workflow Pages

- Pressing `ALERT` opens the alert list immediately.
- With no active alerts, the list shows `NO ACTIVE ALERTS` and does not create
  synthetic entries.
- ALRT lamp stops flashing when `ALERT` is pressed and re-flashes only when a
  newer alert arrives.
- Alert list ordering is newest-first and oldest-last.
- Alert list labels show summary on line 1 and occurrence time as `[HH:MM]` on
  line 2.
- Cursor arrows show when additional alert pages exist and are positioned
  consistently with settings-page arrows.
- `R5=HOME` from alert list returns to home.
- Alert detail page:
  - `R5=ACCEPT` removes selected alert and returns to list.
  - `L5=IGNORE` returns to list without removing selected alert.
  - Cursor arrows scroll additional detail text.

## Failure Capture

When a check fails, capture:

1. One `/preview` screenshot.
2. One panel photo if behaviour differs from preview.
3. Relevant serial lines around the failure.

## Automated Regression (PBM Compare)

Use the host tool to compare `/api/framebuffer.pbm` with a saved baseline:

```bash
python tools/framebuffer_regression.py capture --host merlinccu --out baselines/status_page.pbm
python tools/framebuffer_regression.py compare --host merlinccu --baseline baselines/status_page.pbm --mask 190,0,62,20
```

Notes:

- Use `--mask x,y,width,height` for dynamic regions (for example clock/time).
- Repeat `--mask` for multiple dynamic regions.
- Exit code `0` means pass, `1` means visual regression, `2` means setup/file error.

## Automated Regression Suite

Run a set of checks from one suite file:

```bash
python tools/framebuffer_suite.py capture-all
python tools/framebuffer_suite.py compare-all
```

Default suite file: `tools/framebuffer_suite.json`.
`capture-all` is guided and prompts you page-by-page before each baseline is saved.

Override host or suite:

```bash
python tools/framebuffer_suite.py --host 192.168.1.163 compare-all
python tools/framebuffer_suite.py --suite tools/framebuffer_suite.json compare-all
```

Recommended first-run sequence:

1. Navigate to each requested page when prompted by `capture-all`.
2. Save the baseline set.
3. Re-run `compare-all` without changing pages to confirm a clean pass.
4. Use `compare-all` after UI changes to detect regressions.

