# Proposed GitHub Issues

This document converts the remaining open work themes into issue candidates.
It is intentionally temporary. Once the issues exist in GitHub, this file can
be deleted.

## How To Use

- Treat these as backlog candidates, not a second source of truth.
- Keep the issue tracker as the authoritative location for status, discussion,
  and acceptance updates.
- Prefer one issue per independently testable slice.

## Backlog

### 1. Finalise Physical Keypad Harness And Netlist

- Priority: High
- Labels: `hardware`, `bring-up`, `keypad`
- Why: the keypad wiring is still bench-oriented and unknown pins remain
  unverified.
- Acceptance criteria:
  - every front-panel pin is classified as used, unused, or reserved
  - unknown pins are measured and either mapped or explicitly left unconnected
  - `config/keypad_matrix_config.h` matches the confirmed harness
  - the README hardware notes reflect the final net names

### 2. Bring Up ALRT And TEST Lamp Hardware Drive

- Priority: High
- Labels: `hardware`, `alerting`, `bring-up`
- Why: alert workflow exists in software, but the physical lamps are not final.
- Acceptance criteria:
  - ALRT and TEST lamp outputs are wired and verified
  - lamp state matches the UI and preview surfaces
  - alert-state transitions drive the expected physical indicator

### 3. Add Key And Panel Backlight Control

- Priority: High
- Labels: `hardware`, `ui`, `bring-up`
- Why: user-visible lighting is still provisional.
- Acceptance criteria:
  - key and panel backlight control is implemented for the confirmed hardware
  - the control path is safe and documented
  - UI state changes are reflected consistently

### 4. Implement Photoresistor Or Brightness Sensing

- Priority: Medium
- Labels: `hardware`, `adc`, `bring-up`
- Why: brightness sensing is still speculative but may be needed if retained.
- Acceptance criteria:
  - sensing input is wired to a safe analogue-capable pin
  - readings are stable and bounded
  - UI or configuration surfaces use the measurement if the feature is kept

### 5. Add Home Assistant Calendar Ingestion

- Priority: High
- Labels: `integration`, `calendar`, `feature`
- Why: the calendar UI scaffold exists, but live calendar data is missing.
- Acceptance criteria:
  - calendar events are fetched from Home Assistant
  - events map into the existing calendar model
  - the calendar UI shows live data without breaking existing navigation

### 6. Make Watched Shares Configurable

- Priority: Medium
- Labels: `ui`, `integrations`, `feature`
- Why: share watching is currently hard-coded.
- Acceptance criteria:
  - users can add or remove watched shares through the configured surface
  - the watchlist persists across reboots
  - share pages continue to render correctly for empty and populated lists

### 7. Add Weather Icon Pipeline And Small Icon Set

- Priority: Medium
- Labels: `ui`, `weather`, `rendering`
- Why: weather text exists, but iconography is still planned.
- Acceptance criteria:
  - a small icon set is rendered cleanly in the framebuffer
  - asset size stays within the UI budget
  - preview and physical panel agree on layout

### 8. Add Provider Weather Warning Ingestion

- Priority: Medium
- Labels: `weather`, `alerts`, `integration`
- Why: provider warnings should be surfaced as alerts when available.
- Acceptance criteria:
  - provider warning telemetry is parsed when exposed
  - warnings become operator-facing alerts with clear summaries
  - absent warning telemetry leaves the system stable

### 9. Complete BME280 And BME680 Calibration And Compensated Readings

- Priority: High
- Labels: `sensor`, `hardware`, `bring-up`
- Why: environment sensing still lacks full calibration and compensated data.
- Acceptance criteria:
  - calibration data loads correctly
  - compensated temperature, humidity, and pressure are produced
  - invalid sensor states surface cleanly in diagnostics or alerts

### 10. Define The Multicore Event And Message Boundary

- Priority: High
- Labels: `architecture`, `performance`, `multicore`
- Why: the firmware is still single-core, and a safe split needs a concrete
  boundary.
- Acceptance criteria:
  - ownership between core 0 and core 1 is explicitly documented
  - the communication mechanism is defined
  - early candidate work is identified and excluded from direct UI mutation

### 11. Add Host Tests For Keypad Decode And Alert Ordering

- Priority: Medium
- Labels: `testing`, `host-test`, `alerts`, `keypad`
- Why: current logic would benefit from hardware-independent regression tests.
- Acceptance criteria:
  - keypad closure decoding is covered by host tests
  - alert ordering and acknowledgement behaviour are covered by host tests
  - the tests run without Pico hardware

### 12. Add RAM And Size Budget Tracking

- Priority: Medium
- Labels: `build`, `memory`, `performance`
- Why: static memory growth needs to remain visible during bring-up.
- Acceptance criteria:
  - a size snapshot or RAM budget note exists
  - major static buffers are accounted for
  - new large allocations trigger a deliberate review

### 13. Add A Stale-Data Display Policy

- Priority: Medium
- Labels: `ui`, `alerts`, `resilience`
- Why: outages need a consistent operator-visible treatment.
- Acceptance criteria:
  - stale data is handled consistently across weather, shares, and integration pages
  - the UI indicates age or unavailability in a predictable way
  - the policy is documented in the UI or alerts docs

### 14. Harden Network Trust And Transport Policy

- Priority: Medium
- Labels: `security`, `network`, `hardening`
- Why: the current network stack is still development-friendly.
- Acceptance criteria:
  - HTTPS trust validation is defined and implemented
  - MQTT transport policy is explicit
  - credential and DNS/NTP assumptions are documented or removed

## Suggested Deletion Path

Once the corresponding GitHub Issues exist:

- delete this file
- keep the issue tracker as the active backlog
- keep `README.md` and `docs/*` focused on describing the system

