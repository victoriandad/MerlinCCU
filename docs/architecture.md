# Architecture

## Why This Exists

Capture the stable firmware architecture so future changes do not rely on
README history, chat context, or incidental source-code discovery.

## What Belongs Here

- Runtime ownership boundaries between core, display, input, network, config,
  and sensor modules.
- Data-flow rules for `ConsoleState`, framebuffer ownership, manager snapshots,
  and persistent configuration.
- Constraints specific to Raspberry Pi Pico, Pico SDK, lwIP/CYW43, flash
  writes, PIO, DMA, and multicore work.
- Architecture decisions that affect how new features should be integrated.

## Current Runtime Shape

- `src/core/MerlinCCU.cpp`: startup, main loop, screen saver decisions, display
  present scheduling.
- `src/core/console_controller.cpp`: UI state, softkey routing, alert state,
  runtime config application.
- `src/display/screens.cpp`: shared drawing primitives (detail rows, centred
  text, label wrapping, graph plotting), the `draw_menu_screen()` dispatch,
  and page families not yet split out (issue #45, staged: Status and Weather
  are split into their own files; Calendar/Shares/Pinter/Settings/Alerts
  remain in `screens.cpp` pending the same treatment).
- `src/display/status_screens.cpp`: Status root/overview/connectivity/
  resources/sensors/integrations pages.
- `src/display/weather_screens.cpp`: the live Weather page, its Hourly/Next
  24 Hours/Next 7 Days forecast periods, and forecast-timeline reconstruction.
- `src/display/air_traffic_screens.cpp`: the Local Traffic (ADS-B) page --
  not-configured/waiting states, a paginated Tabular table, and a Plot (PPI
  radar-style) view with per-aircraft snail trails, toggled from the page's
  own softkey (issue #74, `docs/adsb-air-traffic-feature-design.md`).
- `include/display/screens_shared.h`: primitives split-out page-family files
  need from `screens.cpp` (`DetailRow`, `draw_compact_detail_rows`,
  `draw_centered_text`, `text_width`, `WrappedSoftkeyLabel`/`wrap_label_lines`,
  `draw_page_navigation_arrows`, a few state-label lookups) that aren't part
  of the public `screens.h` API.
- `src/display/display.cpp`: framebuffer-to-panel raster composition and DMA/PIO
  presentation.
- `src/core/input.cpp`: physical/provisional keypad polling and logical button
  events.
- `src/network/*`: Wi-Fi, HTTP preview/config, Home Assistant, MQTT, time,
  share-price, and local air-traffic (ADS-B) state machines.
- `src/sensors/*`: optional environment sensor board discovery and sensor
  drivers.

## Ownership Rules

- `console_controller` owns user-facing aggregate state.
- Managers expose compact status snapshots; they do not mutate UI state
  directly.
- `ConsoleState` stays display-facing and should not accumulate raw protocol
  buffers, large histories, calibration tables, or driver internals.
- Draw complete back framebuffers before presenting frames.
- Display state, framebuffer memory, and UI routing remain on the UI owner.

## Memory and Timing Rules

- Prefer bounded arrays and static lifetime where practical.
- Keep rolling histories in the manager that owns the data.
- Sequence or reuse large network buffers before adding new static storage.
- Document new timing assumptions, interrupt/DMA expectations, and flash-write
  constraints.
- Any new large static buffer (roughly 1KB+) is a deliberate review point, not
  a drive-by addition: check it against the Resources status page's tracked
  totals (program flash, static RAM, live heap) before merging.

### Memory budget tracking

The Resources status page (`MenuPage::StatusResources`) is the live source of
truth for size/RAM budget, not this document:

- **Program flash** — linked binary size against `kProgramFlashBudgetBytes`.
- **Static RAM** — `sizeof(ConsoleState)` plus the double UI framebuffer,
  against the 520KB Pico 2 W SRAM total. This is the dominant static
  allocation; anything added to `ConsoleState` shows up here directly.
- **Live heap** — `mallinfo()`'s `uordblks`/`arena`, sampled once a second in
  the main loop alongside the loop-load telemetry (`MerlinCCU.cpp`). Most of
  the firmware avoids the heap deliberately (static arrays throughout), so
  this mainly reflects lwIP/mbedtls allocations from Wi-Fi, MQTT, and the
  (currently disabled) TLS share fetch.
- **Stack headroom** — a canary-filled high-water-mark check, printed once a
  second over serial (see the stack-pressure work from issue #58); not yet
  folded into this status page.

Numbers are load-bearing, not aspirational: check this page (or the serial
stack print) after any change that adds meaningful static or dynamic memory,
rather than assuming headroom exists.

## Multicore Notes

- Current firmware is single application core.
- Do not move work to core 1 until ownership, stack use, flash-write safety,
  and lwIP/CYW43 callback rules are reviewed.
- Safe early candidates are sensor polling, compensation maths, payload
  parsing, and data reduction.

## Environment Sensor Direction

- The Waveshare Pico Environment Sensor board is treated as an optional
  subsystem behind `environment_sensor_manager`.
- I2C configuration is local-machine or harness-specific and lives in
  `config/environment_sensor_config.h`, copied from the example file.
- Sensor drivers should be concrete, allocation-free classes with static
  lifetime where practical.
- The first hardware milestone is bus discovery and a diagnostic snapshot. The
  BME280/BME680 path starts with chip-ID probing before calibration loading and
  compensation maths are added. Full readings should follow as separate
  drivers for BME280/BME680, SGP40, TSL2591, LTR390, and ICM20948.
- SGP40 compensation should consume temperature and humidity from the BME
  sensor when that driver exists.
- Future CCU alerts should be based on compact signals: board missing, stale
  readings, failed compensation, or operator-relevant thresholds.

## Multicore Admission Checklist

Do not move work to core 1 until these are true:

- The work has a single owner and communicates with core 0 through a queue or
  immutable snapshot.
- The work does not write `ConsoleState`, framebuffer memory, display state,
  or Pico SDK flash state directly.
- Stack usage has been estimated for the background task.
- Flash writes are protected so the second core is not executing from flash
  during erase/program operations.
- lwIP/CYW43 calls remain on their current owner unless explicit locking and
  callback-context rules have been reviewed.

## Host-Testable Logic

Some pure, data-only logic is deliberately factored into small headers/TUs
that pull in no Pico SDK headers, so `tests/host/` can exercise it with a
native compiler instead of requiring hardware:

- `include/core/alert_ordering.h` — alert list sort order and
  annunciation/acknowledgement rules, extracted from `console_controller.cpp`.
- `include/core/keypad_matrix_decode.h` — the ribbon-pin-pair-to-button
  table and hit-mask closure test, extracted from `input.cpp`. GPIO wiring
  and the "is this line configured" guard stay in `input.cpp`, since that
  depends on per-board config; `input.cpp` static_asserts that its
  hardware-facing `kObservedLines` pin order matches this header's
  `kObservedPanelPins` so the two tables cannot silently desync.
- `include/core/pinter_scheduling.h` — Pinter brew-pack dock/fridge capacity
  accounting and stage-transition rules, extracted from
  `console_controller.cpp` (#48). `console_controller.cpp` still owns the live
  `PinterStatus` array, softkey labels, and routing; this header only decides
  whether a transition is currently allowed and what it changes.
- `include/core/calendar_navigation.h` — Calendar owner-filter cycling,
  day-offset bounds, and visible-softkey-slot-to-event-index lookup, extracted
  from `console_controller.cpp` (#49). Calendar identity labels
  (`calendar_identity_label`/`calendar_owner_definition`) stay in
  `console_controller.cpp` since they're a configuration concern (reading the
  optional `calendar_identities.h`), not navigation logic. Live Home Assistant
  calendar ingestion (#8, blocked on #33/#34) is out of scope here and will
  feed `ConsoleState.calendar_events` the same way the current seed data does,
  without needing to change this header.

When adding new pure logic to `console_controller.cpp` or `input.cpp`, prefer
extracting it the same way over adding to files that already require Pico
hardware headers to compile.

## Decision Log

| Date | Decision | Reason | Follow-up |
| --- | --- | --- | --- |
| 2026-07-04 | Use a Home Assistant/local proxy feed for share market data instead of direct provider scraping on the Pico. | Google has no supported Pico-friendly Finance REST API, and direct Yahoo chart fetching caused share-page lockup risk. A local feed keeps third-party API keys, large JSON, and provider churn off the device. | Implement #42 before re-enabling live share values. |
| 2026-07-06 | Add `tests/host/`, a native-compiler CMake project covering keypad matrix decode and alert ordering/acknowledgement, extracted into hardware-independent headers. | Closes issue #14; both areas were previously only reachable through hand testing on real hardware. | Extend the same pattern to other pure logic (e.g. Pinter scheduling, #48) as it's identified. |
| 2026-07-08 | Extract Calendar owner-filter/day-navigation/slot-selection logic into `calendar_navigation.h`, following the #48 Pinter-scheduling split. | Closes issue #49; keeps this logic reviewable and host-testable ahead of the #8/#33/#34 Home Assistant calendar ingestion work landing in the same area. | Split `console_controller.cpp` further per #44 (softkey label construction, status ingestion remain inline). |
| 2026-07-08 | Split the Status page family (root/overview/connectivity/resources/sensors/integrations) out of `screens.cpp` into `status_screens.cpp`, with cross-family primitives promoted into `screens_shared.h`. | Progresses issue #45 as a staged refactor (per the #3 housekeeping notes) rather than one large rewrite; `screens.cpp` dropped from 3319 to 2817 lines with no rendering change. | Repeat the same split for Weather, Calendar, Shares, Pinter, Settings, and Alerts page families. |
| 2026-07-08 | Simplified `set_home_assistant_status`/`set_mqtt_status` in `console_controller.cpp` to use the `operator!=` added for issue #67, instead of re-deriving the same field-by-field comparison inline; did not further split `console_controller.cpp` (#44) beyond this. | The remaining candidates (softkey label construction, status-snapshot ingestion, settings routing) all read/write `g_console_state` directly (463 references in the file) and call private helpers like `update_softkeys_from_state()` -- unlike the #48/#49 splits, which extracted logic that already took explicit parameters instead of touching the global, these would need to either accept a much bigger `ConsoleState&`-threading refactor or expose more of the controller's private surface across a TU boundary. Neither is a small staged step. | Revisit #44 only alongside a deliberate decision on whether `ConsoleState` should be passed explicitly through more of the call chain -- forcing a module split before that decision would just relocate tightly-coupled code, not separate concerns. |
| 2026-07-09 | Split the Weather page family (live weather page, forecast periods, forecast-timeline reconstruction) out of `screens.cpp` into `weather_screens.cpp`; promoted `text_width`, `draw_centered_text`, `WrappedSoftkeyLabel`/`wrap_label_lines`, and `draw_info_page_rows` into `screens_shared.h` since Weather needed them too. | Continues issue #45's staged split. `screens.cpp` dropped from 2817 to 2052 lines with no rendering change (verified via clean firmware rebuild). | Repeat the same split for Calendar, Shares, Pinter, Settings, and Alerts page families. |
| 2026-07-09 | Implemented the list-view milestone of the local air-traffic (ADS-B) feature: `air_traffic_manager.cpp` (plain-TCP altcp state machine against adsb.lol's `/v2/point/...` endpoint, bounded JSON parsing, top-N-by-distance selection) and `air_traffic_screens.cpp` (compact callsign/distance/altitude/bearing table), gated to its own `MenuPage::AirTraffic` page for both display and network fetch (page-isolated, same as share-price fetches). | Closes out the first milestone of issue #74's pre-existing design doc, which explicitly deferred the radar-style visualization in favour of a compact list page built on the established non-blocking manager shape. | Consider the radar-style visualization as a follow-up milestone if the list view proves useful; open question remains whether the `Api-Auth` header name used for private ADS-B feeds is correct (unconfirmed against a real key). |
| 2026-07-09 | Removed the local web config page's admin-password gate entirely (`admin_password`/`require_admin_password` fields, the `SecuritySettings` on-device page, and the per-save password check in `web_config_server.cpp`); promoted the still-needed "Remote Config" (whether the web server runs at all) toggle directly onto the main Settings page. | User request: on a home-network device the per-save password prompt was pure friction with no real threat model behind it, and it was actively causing confusing silent-looking save failures (a wrong/blank password rejects the *entire* form, not just the field being changed). `LegacyRuntimeConfigV1`'s admin_password/require_admin_password fields are kept as-is since that struct is a frozen historical migration snapshot. | If remote (outside-LAN) access is ever wanted, access control will need to be reconsidered from scratch -- there is now no gate at all beyond the Remote Config on/off toggle. |
| 2026-07-09 | Widened the ADS-B response buffer 8KB to 16KB; raised tracked/displayed aircraft from 10 to 24 with an 8-per-page Tabular view (paged via the cursor keys, mirroring the Alert list/`kAlertsPerPage` pattern, including the header's "ADS-B TRAFFIC N/M" convention borrowed from Settings/Alerts); added a Plot view (PPI-style range rings, compass tick, per-aircraft blips with a 10-refresh snail trail) toggled from the page's own `L5` softkey (`AirTrafficViewMode`, `ToggleAirTrafficViewMode`). Trail history is tracked in `air_traffic_manager.cpp` keyed by ICAO24 hex (`g_tracked`), independent of the display-facing top-N selection, and cleared whenever an aircraft drops out of a fetch cycle rather than decayed gradually. | User request, following on from the radius/truncation bug fix -- a wider radius is only useful if there's a way to see and page through more than 10 aircraft, and a spatial "where are they" view was requested as a second presentation alongside the existing table. | Total static RAM (`.bss`) is now ~498.5KB of the 520KB budget (~33.9KB free, down from ~45.6KB before this session) -- worth checking against the Resources page after future additions; the radar view has no aircraft-label decluttering (dense airspace + 24 blips can overlap), which is an accepted simplification for now. |
| YYYY-MM-DD |  |  |  |
