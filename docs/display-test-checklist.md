
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

## Weather Page

- `PERIOD` softkey cycles `Hour` -> `Day` -> `Week` and wraps back to `Hour`.
- Forecast table columns align (`Time`, `Temp`, `Wind mph`, `Conditions`).
- `Day` period uses spaced rows with condition sub-lines and no clipped text.
- `Week` period shows provider daily rows (typically up to 7 entries) with
  labels (`Today`, `Tmrw`, then date labels like `DD/MM`).
- Times are local-time oriented and progress sensibly from the current period.
- Sunrise and sunset are both present when provider data is available.
- Source footer is visible and readable.

## Settings Pages

- Softkey top-line captions are uppercase.
- Bracketed selected values remain mixed case.
- Selection changes update visible values immediately.

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

