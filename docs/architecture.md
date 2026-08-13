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
- **Static RAM** — real `.data`+`.bss` usage, read directly from the linked
  image via the Pico SDK linker script's `__end__` symbol (end of `.bss`,
  i.e. heap start) against `__StackLimit` (top of the linker's usable RAM
  region), both resolved in `status_screens.cpp`'s `static_ram_bytes()`/
  `total_ram_bytes()`. The usable RAM region is **512KiB, not the RP2350's
  520KB physical SRAM total** -- SRAM8/SRAM9 (8KiB) sit outside the
  contiguous `RAM` output section the default linker script maps, so they're
  never linked into and don't count as usable static+heap+stack space.
  (Previously this row only summed `sizeof(ConsoleState)` plus the double UI
  framebuffer against a hardcoded "520KB" -- ~48KB against ~495KB actually
  used, a nearly 10x undercount that made the page look like it had far more
  headroom than it did. Fixed under issue #15; the `CONSOLE`/`UI BUFFERS`
  rows still show that sub-breakdown, they just no longer stand in for the
  total.) `tools/check_size_budget.py` reproduces the same figures from a
  built `.elf`/`.map` outside the device, for a pre-flash/pre-merge check.
- **Live heap** — `mallinfo()`'s `uordblks`/`arena`, sampled once a second in
  the main loop alongside the loop-load telemetry (`MerlinCCU.cpp`). Most of
  the firmware avoids the heap deliberately (static arrays throughout), so
  this mainly reflects lwIP/mbedtls allocations from Wi-Fi, MQTT, and the
  (currently disabled) TLS share fetch. Now shown on the Resources page
  (`HEAP LIVE` row) instead of only over serial.
- **Stack headroom** — a canary-filled high-water-mark check
  (`stack_high_water_free_bytes()` in `MerlinCCU.cpp`; see the stack-pressure
  work from issue #58), sampled alongside heap/loop-load and now also shown
  on the Resources page (`STACK FREE` row, exact bytes rather than
  KiB-rounded since core-0's stack is only 4KB) as well as printed once a
  second over serial.

#### Major static buffers

Read from `build/MerlinCCU.elf.map` (2026-08-12 build), largest first, everything
2KB and over. This is a snapshot for orientation, not a live figure -- re-derive
it from a fresh `.map` if it matters for a specific decision:

| Buffer | Size | Notes |
| --- | --- | --- |
| `screensaver_life::g_life_a` + `g_life_b` | 39.4 KiB (2 x 19.7 KiB) | Conway's Life double-buffer; larger than `ConsoleState`, only live while that screensaver is selected. |
| `web_config_server::g_response` | 24.0 KiB | Whole rendered HTML config page assembled in one buffer. |
| `console_controller::g_console_state` | 27.0 KiB | The `ConsoleState` instance itself -- see the review rule below. |
| `framebuffer::g_fb_a` + `g_fb_b` | 20.0 KiB (2 x 10.0 KiB) | Double UI framebuffer, 252x320 1bpp. |
| `home_assistant_manager::g_response` | 16.0 KiB | Always reserved even when HA is disabled -- gated by `RuntimeConfig::home_assistant_enabled` (a runtime flag), not a `constexpr`, so the compiler can't prove the buffer unreachable and drop it. |
| `air_traffic_manager::g_response` | 16.0 KiB | Same story as HA's buffer, gated by `air_traffic_enabled`. |
| `air_traffic_manager::g_status` (`AirTrafficStatus`) | 2.0 KiB | Tracked-aircraft snapshot, `kAirTrafficEntryCapacity` entries. |
| `environment_sensor_manager::g_status` | 2.1 KiB | BME280/SGP40 sensor history buffers. |

Notably absent: `share_price_manager`'s `g_response`/`g_request` buffers
(16KB + 768B in source) don't appear in `.bss` at all. `kEnableLiveShareFetch`
is a `constexpr bool` set to `false`, so the compiler can prove the only code
that touches those buffers is unreachable and drops both the code and the
backing storage entirely -- unlike HA/ADS-B's runtime-gated equivalents
above, a `constexpr`-gated feature costs nothing while disabled.

Numbers are load-bearing, not aspirational: check this page (or run
`tools/check_size_budget.py` against a fresh build) after any change that
adds meaningful static or dynamic memory, rather than assuming headroom
exists. Any new large static buffer (roughly 1KB+) is a deliberate review
point per the rule above -- with the static-RAM figure now accurate, that
check actually reflects reality.

## Multicore Notes

- Current firmware is single application core.
- Do not move work to core 1 until ownership, stack use, flash-write safety,
  and lwIP/CYW43 callback rules are reviewed.
- Safe early candidates are sensor polling, compensation maths, payload
  parsing, and data reduction.
- A narrower, display-only candidate is also under design: dedicating core 1
  to per-frame raster regeneration for temporal dithering (see
  `docs/multicore-raster-regen-design.md`). This is a different shape of
  multicore work than the network-offload direction above — core 1 would own
  a small, mechanical task (rebuild dirty raster lines from a per-pixel level
  buffer, once per physical frame) rather than sharing in UI or network
  logic. It is a candidate specifically because its interface is narrow (one
  small input buffer, one output buffer) compared to migrating any part of
  lwIP/CYW43. See the admission-checklist exception below before treating
  this as precedent for other display-state work.

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

## Stale-Data Display Policy

Every data-backed page (Weather, Shares, Air Traffic, the Status Integrations
and Sensors pages) shows freshness the same way, through one shared helper --
issue #16. Before this, each page had invented its own vocabulary: shares said
"Live"/"Demo", the Integrations page said "Valid"/"-", Air Traffic said
"WAITING FOR DATA"/"NO AIRCRAFT NEARBY", and Weather showed no staleness
indicator at all -- a silently-stuck feed looked identical to a live one.

- **`screens::build_data_freshness_text(currently_valid, last_success_ms,
  now_ms, stale_after_ms, out, out_size)`** (`screens.cpp`, declared in
  `screens_shared.h`) is the one place this vocabulary lives: `"LIVE"`,
  `"STALE 12m"`, or `"NO DATA"`. `last_success_ms`/`now_ms` are boot-uptime
  milliseconds (`to_ms_since_boot`), matching the pre-existing
  `EnvironmentSensorStatus::bme_last_read_ms` idiom -- not wall-clock, so it
  works before time sync. `stale_after_ms` is each subsystem's own threshold
  (this codebase uses 4x that subsystem's own refresh interval throughout).
  A companion `screens::build_elapsed_time_text(elapsed_ms, out, out_size)`
  formats the compact "Xs"/"Xm"/"Xh" age text on its own, reused by the
  Sensors page's `ENV SCAN` row (previously showed a raw uptime timestamp,
  not an actual age -- fixed as part of the same pass).
- Each of `HomeAssistantStatus` (`weather_last_success_ms`),
  `ShareMarketStatus` (`last_success_ms`), and `AirTrafficStatus`
  (`last_success_ms`) gained a boot-uptime timestamp, stamped at the genuine
  success point in each manager (e.g. only once weather condition JSON
  actually parses, not on every HTTP 200). MQTT was deliberately left out --
  it is a publish target with a connection-state concept, not a
  freshness/data-received one, so its existing Connected/Disconnected state
  already covers "unavailability" per the issue's requirement.
- **Deliberate placeholder data does not use this helper.** Shares before
  issue #42's local feed lands always show `"DEMO"` (detected by
  `last_success_ms == 0`, i.e. the live path has never run since
  `kEnableLiveShareFetch` is `false`) -- that's an intentional deployment
  state, not an outage, and conflating it with "NO DATA" would be misleading
  in the other direction.
- **Timestamp fields are excluded from every status struct's change
  comparator** (`operator==` in `console_model.h`, or the equivalent
  hand-written comparator in `share_price_manager.cpp`) so a timestamp
  advancing alone doesn't force a redraw on every single main-loop tick.
  That means the setters that mirror manager status into `ConsoleState`
  (`console_controller.cpp`'s `set_home_assistant_status()`,
  `set_air_traffic_status()`, `set_share_market_status()`) must copy the
  timestamp field through unconditionally, *before* the change-detection
  early return -- otherwise it would never reach `ConsoleState` on ticks
  where nothing else changed. Follow that same shape for any future status
  struct that adds a freshness timestamp.
- Since freshness pages can go stale with zero other field changes, they
  need a redraw even when nothing else moved. `MerlinCCU.cpp` has a 30-second
  `next_freshness_redraw` tick (matching the existing 30s heartbeat's
  granularity) that requests a redraw while the active page is one of
  Weather/Shares/ShareDetail/AirTraffic/StatusIntegrations/StatusSensors --
  coarse enough to add no meaningful redraw churn, fine enough that the age
  label doesn't look frozen.

## Network Helper Ownership Boundary

Issue #46 extracted `include/network/http_response.h` /
`src/network/http_response.cpp` from what used to be three independently-
converged copies of the same HTTP response parsing in
`home_assistant_manager.cpp`, `air_traffic_manager.cpp`, and
`share_price_manager.cpp`. The boundary is deliberately narrow:

- **In `http_response`:** status-line parsing (`partial_status`), header
  block detection (`headers_end`/`body`), named-header lookup
  (`find_header_value`/`header_has_token`), `Content-Length` parsing
  (`parse_content_length`), chunked-transfer decoding (`decode_chunked_body`),
  and buffered-completion detection (`is_complete`, letting a manager act as
  soon as `Content-Length` or a terminated chunked body is present instead of
  waiting for the connection to close). Every function takes an explicit
  buffer and length rather than reading a manager's own global response
  buffer -- host-tested in `tests/host/test_http_response.cpp`, no lwIP/Pico
  SDK dependency.
- **Explicitly NOT here, and why:**
  - **TLS trust policy** -- covered by issue #17, not this one.
  - **Provider-specific JSON body parsing** (`extract_json_string`-style
    functions, and the bounded array-scanning helpers
    `find_object_end`/`extract_bounded_string`/`extract_bounded_number`
    duplicated between `air_traffic_manager.cpp` and
    `share_price_manager.cpp`) -- deliberately left alone. Consolidating
    those too would have coupled this change to a second, independent
    refactor with its own risk; a future pass can revisit it if it's still
    worth doing once more managers exist to compare against.
  - **The socket/DNS/retry state machine and completion-handling shape** --
    each manager keeps its own. `home_assistant_manager.cpp` processes a
    completed response **inline** from within its `on_tcp_recv` callback
    (calling `handle_http_status()` directly, including the PCB
    teardown/reset that implies); `air_traffic_manager.cpp` and
    `share_price_manager.cpp` instead **defer** completion
    (`defer_completion()`/`g_completion_pending`) to the next `update()`
    tick, with an explicit comment (predating this issue) that lwIP
    callbacks must not close or replace the active PCB while lwIP is still
    unwinding the callback -- a hard-won fix applied after the original
    Yahoo-fetch lockup (#19). Unifying these two shapes was considered and
    rejected for this pass: it isn't clear whether HA's inline path is safe
    because HA's specific request sequencing never exercises the reentrancy
    window, or whether it's a latent version of the same bug that just
    hasn't been hit yet -- not something to resolve as a side effect of a
    parsing-helper extraction. Each manager now gets more correct, shared
    *parsing* (in particular, `air_traffic_manager.cpp` and
    `share_price_manager.cpp` gained early buffered-completion detection via
    `is_complete()`, and `air_traffic_manager.cpp` gained chunked-body
    dechunking it previously didn't have at all) without touching *when* or
    *how* each one decides a fetch is done.

## Weather Parsing Ownership Boundary

Issue #47 split `home_assistant_manager.cpp` (previously ~3550 lines mixing
Home Assistant REST sequencing, direct Open-Meteo fetching, HTTP framing
(already extracted per #46), JSON extraction, unit/condition normalisation,
and forecast/alert-metric aggregation) into five pure, host-testable modules
plus the transport file itself:

- **`weather_json_scan.h`/`.cpp`** -- generic-shaped JSON scanning primitives
  used by both weather providers: quoted-string/scalar field extraction,
  brace-matching (string-aware, so descriptive text containing `{`/`}`
  doesn't confuse it), and array element lookup by index. Not a JSON library
  -- it scans a flat or lightly-nested payload for one named field/array at a
  time, matching what these providers' responses actually look like.
- **`weather_normalisation.h`/`.cpp`** -- provider-neutral unit conversion
  (temperature C/F, wind speed km/h-m/s-ft/s-kn-Beaufort all to mph),
  condition-code-to-label mapping for *both* providers
  (`friendly_weather_condition` for Home Assistant's string codes,
  `open_meteo_condition_from_code` for Open-Meteo's numeric ones -- kept as
  two functions since the vocabularies don't overlap), and display text
  formatting (compass bearings, compact wind/temperature-range strings, hour/
  date extraction from ISO datetimes).
- **`weather_forecast_parser.h`/`.cpp`** -- forecast/alert-metric aggregation
  operating on an explicit `HomeAssistantStatus&`: the `WeatherMetrics`/
  `WeatherAlertStatus` side-channel used by threshold alerts (min/max
  temperature, max wind speed, severe-condition warnings), plus Home
  Assistant's own `forecast` attribute-array parsing (hourly and daily
  variants -- Open-Meteo's forecast parsing is separate, see below).
- **`home_assistant_weather_parser.h`/`.cpp`** -- Home Assistant-specific:
  weather-source-hint derivation (attribution/friendly_name), sun entity
  parsing, and `parse_current_weather_entity()`. That last one is notable:
  unlike Open-Meteo's parsing (which was already a dedicated function), HA's
  current-weather-entity parsing had no function of its own at all -- it was
  ~90 lines inlined directly inside `handle_http_status()`, the transport
  file's lwIP receive-completion callback. Issue #47 pulled it out into a
  real function; `handle_http_status()` now just calls it and stamps
  `weather_last_success_ms` (a hardware-clock read, deliberately kept in the
  transport file so the parser itself stays host-testable) based on its
  return value.
- **`open_meteo_parser.h`/`.cpp`** -- `open_meteo::parse_weather()`, the
  combined current+hourly+daily Open-Meteo response parser. Kept as its own
  module rather than merged with the Home Assistant parser (issue #47's own
  scoping) since the two providers' payload shapes and unit-handling only
  partially overlap; both share the primitives above instead.

All five take explicit parameters (`HomeAssistantStatus&`, unit-marker
chars/buffers) instead of reading `home_assistant_manager.cpp`'s file-static
globals, and none depend on lwIP/Pico SDK headers -- host-tested in
`tests/host/test_weather_*.cpp`. One exception worth noting:
`weather_normalisation::format_hour_text()` calls
`time_manager::format_local_time_from_iso8601()` to preserve the original
local-time-conversion-first behaviour exactly; `time_manager.cpp` itself
isn't host-buildable (it includes `pico/stdlib.h`), so
`tests/host/stubs/time_manager_stub.cpp` provides a host-only stand-in that
always reports "not converted," which exercises `format_hour_text()`'s real
documented UTC-fallback path rather than faking anything.

`home_assistant_manager.cpp` itself kept: the connect/send/recv/retry state
machine, HTTP status-line/header wrappers (from #46), request-kind
sequencing, endpoint configuration, and `clear_runtime_data()` (an
orchestration function that clears connection-lifecycle state alongside
calling into the parsing modules' own clear functions -- it stayed here
since it's fundamentally about *when* to forget stale data on disconnect, not
about parsing). After the split, roughly 30 of the ~50 wrapper-style
functions this file used to define were themselves deleted rather than kept
as unused pass-throughs, once their only callers turned out to be the code
that moved into the new modules -- see the 2026-08-13 (#47) Decision Log
entry for why that's a larger diff than issue #46's all-wrappers-kept
precedent.

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

**Exception under design:** the raster-regeneration candidate above
necessarily writes display state (the raster back-buffer) from core 1,
which the second checklist item forbids in general. This is treated as a
deliberate, narrow exception rather than a relaxation of the rule: core 1
would own *only* the raster buffer's dirty-line contents, under the same
producer/consumer discipline the DMA control-channel IRQ already uses today
for content-change adoption, and would not touch `ConsoleState`, softkeys, or
any other UI/display state. This exception applies to that specific design
only, not to display-state work in general, until it has been implemented
and reviewed on hardware.

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
| 2026-07-05 | Added a narrow multicore exception for a dedicated raster-regeneration core (core 1), instead of storing multiple pre-built native raster buffers, to make limited temporal dithering RAM-feasible. | Static RAM is already at ~467 KB of 520 KB (measured via `arm-none-eabi-size` on the compiled firmware), so storing 2+ additional ~98 KB raster buffers for temporal-dithering phases is not viable; regenerating on the fly from a small per-pixel level buffer avoids that cost but needs a core dedicated to it so it doesn't compete with Wi-Fi/HTTP/keypad work on core 0. | Measure actual frame rate and raster-rebuild time on hardware (new instrumentation on the Resources status page) before implementing; see `docs/multicore-raster-regen-design.md`. |
| 2026-07-06 | Add `tests/host/`, a native-compiler CMake project covering keypad matrix decode and alert ordering/acknowledgement, extracted into hardware-independent headers. | Closes issue #14; both areas were previously only reachable through hand testing on real hardware. | Extend the same pattern to other pure logic (e.g. Pinter scheduling, #48) as it's identified. |
| 2026-07-08 | Extract Calendar owner-filter/day-navigation/slot-selection logic into `calendar_navigation.h`, following the #48 Pinter-scheduling split. | Closes issue #49; keeps this logic reviewable and host-testable ahead of the #8/#33/#34 Home Assistant calendar ingestion work landing in the same area. | Split `console_controller.cpp` further per #44 (softkey label construction, status ingestion remain inline). |
| 2026-07-08 | Split the Status page family (root/overview/connectivity/resources/sensors/integrations) out of `screens.cpp` into `status_screens.cpp`, with cross-family primitives promoted into `screens_shared.h`. | Progresses issue #45 as a staged refactor (per the #3 housekeeping notes) rather than one large rewrite; `screens.cpp` dropped from 3319 to 2817 lines with no rendering change. | Repeat the same split for Weather, Calendar, Shares, Pinter, Settings, and Alerts page families. |
| 2026-07-08 | Simplified `set_home_assistant_status`/`set_mqtt_status` in `console_controller.cpp` to use the `operator!=` added for issue #67, instead of re-deriving the same field-by-field comparison inline; did not further split `console_controller.cpp` (#44) beyond this. | The remaining candidates (softkey label construction, status-snapshot ingestion, settings routing) all read/write `g_console_state` directly (463 references in the file) and call private helpers like `update_softkeys_from_state()` -- unlike the #48/#49 splits, which extracted logic that already took explicit parameters instead of touching the global, these would need to either accept a much bigger `ConsoleState&`-threading refactor or expose more of the controller's private surface across a TU boundary. Neither is a small staged step. | Revisit #44 only alongside a deliberate decision on whether `ConsoleState` should be passed explicitly through more of the call chain -- forcing a module split before that decision would just relocate tightly-coupled code, not separate concerns. |
| 2026-07-09 | Split the Weather page family (live weather page, forecast periods, forecast-timeline reconstruction) out of `screens.cpp` into `weather_screens.cpp`; promoted `text_width`, `draw_centered_text`, `WrappedSoftkeyLabel`/`wrap_label_lines`, and `draw_info_page_rows` into `screens_shared.h` since Weather needed them too. | Continues issue #45's staged split. `screens.cpp` dropped from 2817 to 2052 lines with no rendering change (verified via clean firmware rebuild). | Repeat the same split for Calendar, Shares, Pinter, Settings, and Alerts page families. |
| 2026-07-09 | Implemented the list-view milestone of the local air-traffic (ADS-B) feature: `air_traffic_manager.cpp` (plain-TCP altcp state machine against adsb.lol's `/v2/point/...` endpoint, bounded JSON parsing, top-N-by-distance selection) and `air_traffic_screens.cpp` (compact callsign/distance/altitude/bearing table), gated to its own `MenuPage::AirTraffic` page for both display and network fetch (page-isolated, same as share-price fetches). | Closes out the first milestone of issue #74's pre-existing design doc, which explicitly deferred the radar-style visualization in favour of a compact list page built on the established non-blocking manager shape. | Consider the radar-style visualization as a follow-up milestone if the list view proves useful; open question remains whether the `Api-Auth` header name used for private ADS-B feeds is correct (unconfirmed against a real key). |
| 2026-07-09 | Removed the local web config page's admin-password gate entirely (`admin_password`/`require_admin_password` fields, the `SecuritySettings` on-device page, and the per-save password check in `web_config_server.cpp`); promoted the still-needed "Remote Config" (whether the web server runs at all) toggle directly onto the main Settings page. | User request: on a home-network device the per-save password prompt was pure friction with no real threat model behind it, and it was actively causing confusing silent-looking save failures (a wrong/blank password rejects the *entire* form, not just the field being changed). `LegacyRuntimeConfigV1`'s admin_password/require_admin_password fields are kept as-is since that struct is a frozen historical migration snapshot. | If remote (outside-LAN) access is ever wanted, access control will need to be reconsidered from scratch -- there is now no gate at all beyond the Remote Config on/off toggle. |
| 2026-07-09 | Widened the ADS-B response buffer 8KB to 16KB; raised tracked/displayed aircraft from 10 to 24 with an 8-per-page Tabular view (paged via the cursor keys, mirroring the Alert list/`kAlertsPerPage` pattern, including the header's "ADS-B TRAFFIC N/M" convention borrowed from Settings/Alerts); added a Plot view (PPI-style range rings, compass tick, per-aircraft blips with a 10-refresh snail trail) toggled from the page's own `L5` softkey (`AirTrafficViewMode`, `ToggleAirTrafficViewMode`). Trail history is tracked in `air_traffic_manager.cpp` keyed by ICAO24 hex (`g_tracked`), independent of the display-facing top-N selection, and cleared whenever an aircraft drops out of a fetch cycle rather than decayed gradually. | User request, following on from the radius/truncation bug fix -- a wider radius is only useful if there's a way to see and page through more than 10 aircraft, and a spatial "where are they" view was requested as a second presentation alongside the existing table. | Total static RAM (`.bss`) is now ~498.5KB of the 520KB budget (~33.9KB free, down from ~45.6KB before this session) -- worth checking against the Resources page after future additions; the radar view has no aircraft-label decluttering (dense airspace + 24 blips can overlap), which is an accepted simplification for now. |
| 2026-07-10 | Fixed a stack-overflow bug found via live hardware debugging: `console_controller::init()`'s `g_console_state = make_default_console_state();` returned the ~27KB `ConsoleState` by value into an *assignment* (not a direct-initialization the compiler can elide), which required materializing the full struct as a stack temporary -- disassembly confirmed a 27,636-byte stack frame against a 2KB (`PICO_STACK_SIZE`) stack. Converted `make_default_console_state()` to write into a `ConsoleState&` out-parameter instead; also doubled the core-0 stack (`PICO_STACK_SIZE=0x1000` in `CMakeLists.txt`, set before `pico_sdk_init()`) since the live stack-depth canary read exactly 0 bytes free even after the fix. | The CCU stopped booting entirely (silent hang, no serial output past config load) after the ADS-B Tabular/Plot session further grew `ConsoleState`, tipping a pre-existing latent bug into an actual failure. Root-caused via targeted `BOOT:`/`stdio_flush()` checkpoints added temporarily to MerlinCCU.cpp/config_manager.cpp (removed once the call site was found) and confirmed with `arm-none-eabi-objdump` disassembly, not guesswork. | The core-0 stack lives in a dedicated 4KB RP2350 scratch bank (`SCRATCH_Y`), not the general SRAM pool, so 0x1000 is the practical ceiling without custom linker-script surgery; re-check the stack-depth canary after any future change that adds deep call chains or large locals. Grep for other `= some_large_struct_by_value_function();` assignment patterns if a similar hang recurs elsewhere. |
| 2026-08-12 | Made the shares watchlist user-configurable (issue #9): `WatchedShareConfig` (symbol + display name) and `watched_share_count` added to `RuntimeConfig`, edited via a new "Watched Shares" section on the web config form (up to `kMaxWatchedShares` = 6 rows, blank symbol removes a row); `share_price_manager` now seeds its placeholder rows from `config_manager::settings()` every tick instead of a hardcoded `BA.L`/"BAE SYSTEMS" single entry, and the on-device Shares page now builds one `SelectShareSlotN` softkey per configured share (was hardcoded to slot 0 only), matching the existing Pinter/Alert multi-slot softkey pattern. Config version bumped 2->3 with a new `LegacyRuntimeConfigV2`/`migrate_legacy_settings_v2` snapshot of the pre-#9 `RuntimeConfig` shape, since the struct had grown in place since v2 without a version bump (undocumented gap found while implementing this). | Share watching was previously hard-coded to a single stock with no add/remove path anywhere, and the Shares page's per-share softkey wiring only ever addressed slot 0 even though the underlying data model was already a 6-entry array. On-device text entry for arbitrary stock symbols was judged out of scope (no generic alpha keypad text-entry subsystem exists yet -- `LetterMode` only drives a header icon, it isn't wired to any editable buffer), so add/remove goes through the web form, consistent with how Wi-Fi/HA/MQTT/ADS-B config is already edited today. | Live multi-symbol fetching stays out of scope (tracked by #42's local-feed replacement); only watched_shares[0] is ever live-fetched if `kEnableLiveShareFetch` is turned back on. If an on-device add/remove flow is wanted later, it needs a generic keypad text-entry state machine first (the coordinate_editor.cpp pattern from #87 only handles numeric lat/long, not arbitrary alpha symbols). |
| 2026-08-12 | Added RAM/size budget tracking (issue #15): the Resources status page's "STATIC RAM" figure now reads real linked `.data`+`.bss` usage from Pico SDK linker symbols (`__end__`, `__StackLimit`) instead of just `sizeof(ConsoleState)` plus the double UI framebuffer; the "HEAP LIVE" row (previously hardcoded `-` despite `HeapStatus` already being tracked) and a new "STACK FREE" row (from the existing `stack_high_water_free_bytes()` canary scan, now also sampled into `ConsoleState` alongside heap/loop-load) are both wired up; added `tools/check_size_budget.py` to reproduce the same flash/RAM figures from a built `.map` file outside the device, replacing the ad hoc `arm-none-eabi-size` runs previously pasted into this log by hand (see the 2026-07-05 and 2026-07-09 entries above). | The old static-RAM figure was a proxy that had drifted badly from reality: it showed ~48KB against a hardcoded "520KB" budget (implying ~91% free) when real `.data`+`.bss` usage was ~495KB -- a ~10x undercount that made the documented review rule ("check any new 1KB+ static buffer against the Resources page's tracked totals") check a number disconnected from the real constraint. Also found in the process: the RP2350's usable linked `RAM` region is 512KiB, not the chip's 520KB physical SRAM total -- SRAM8/SRAM9 (8KiB) sit outside the linker's contiguous `RAM` output section, so the long-used "520KB" budget denominator was never quite right either. | Real headroom as of this change is ~17.5KB free (506.7KB of 512KB, 96.7%) -- run `tools/check_size_budget.py` before adding any further static storage; several open issues (#10 weather icons, #56 core-1 raster regen buffers) plan meaningful new static allocation and will need to budget against this real number, not the old proxy. Stack headroom is now visible on-device too but still per-core-0 only; no equivalent exists for a future core-1 stack if #56 proceeds. |
| 2026-08-13 | Added a consistent stale-data display policy across Weather, Shares, Air Traffic, and the Status Integrations/Sensors pages (issue #16): one shared `screens::build_data_freshness_text()` helper producing "LIVE"/"STALE Xm"/"NO DATA"; new boot-uptime `last_success_ms`-style timestamp fields on `HomeAssistantStatus`/`ShareMarketStatus`/`AirTrafficStatus`, stamped at each manager's genuine success point; a new 30s `next_freshness_redraw` tick in `MerlinCCU.cpp` so the age text keeps advancing even when no other field changes. See the new "Stale-Data Display Policy" doc section above for the full contract. | Before this, four different pages used four different vocabularies for the same underlying problem ("Live"/"Demo", "Valid"/"-", "WAITING FOR DATA"/"NO AIRCRAFT NEARBY", and Weather showing no staleness indicator at all -- a silently-stuck feed looked identical to a live one). Scoped as the "full policy" option rather than a smaller MVP after user confirmation, since a partial fix would have left the same inconsistency for whichever subsystem was skipped. | MQTT was deliberately left out (connection-state concept, not a data-freshness one). Shares' freshness plumbing is currently inert in practice -- `last_success_ms` only advances once `kEnableLiveShareFetch` is re-enabled (#42) -- but the wiring is in place so that transition doesn't need a second pass through every display page. |
| 2026-08-13 | Implemented the local Home Assistant share-price feed (issue #42), replacing the dead Yahoo TLS fetch path in `share_price_manager.cpp` with a plain-TCP local-endpoint client modeled on `air_traffic_manager.cpp`'s connect/request/parse skeleton (no TLS needed for a local fetch). Reuses `home_assistant_host`/`home_assistant_port` per the issue's proposed contract, gated by a new independent `shares_feed_enabled`/`shares_feed_token` config pair (mirroring how `air_traffic_enabled`/`air_traffic_api_key` stay independent of the HA section). Parses `{"shares":[...]}` with the same bounded, string-aware JSON-array approach `air_traffic_manager.cpp` already uses for `{"ac":[...]}`, matching by `symbol` against the issue #9 watchlist so every configured share updates from one request, not just index 0 as the old Yahoo path did. `ShareMarketStatus::last_success_ms` (added for issue #16) now becomes live once a fetch actually succeeds, so the Weather/Shares/Integrations freshness UI needed no further changes. Config version bumped 3->4 with a `LegacyRuntimeConfigV3` snapshot. See `docs/share-feed-design.md` for the full endpoint contract, Home Assistant setup sketch, and provider recommendations. | Direct third-party fetching (the original Yahoo path) caused a share-detail lockup (#19) and was disabled pending exactly this replacement -- see the 2026-07-04 decision entry above. `air_traffic_manager.cpp` was the better template than `home_assistant_manager.cpp` for a NEW plain-HTTP client: no TLS complexity, no dual-mode (HA-entity vs direct-provider) branching to thread through, and it already had the exact bounded-JSON-array parsing shape (`find_object_end`, bounded `extract_bounded_string`/`extract_bounded_number`) this needed for a `{"shares":[...]}` array instead of the old single-object Yahoo response. | Duplicates the bounded-JSON helpers `air_traffic_manager.cpp` already has rather than sharing them -- deliberate, since issue #46 ("Extract Shared Bounded HTTP Client And Response Parser") is the tracked follow-up for that consolidation, and doing it as a drive-by here would have coupled two otherwise-independent changes. Also shrunk `share_price_manager.cpp`'s response buffer 16KB->4KB (the old Yahoo chart payload needed the headroom, the new compact multi-share JSON doesn't) to help the tight RAM budget from #15, but total static RAM still grew ~5.7KB net on this branch (new config/manager fields elsewhere) -- not fully root-caused per-symbol, but `tools/check_size_budget.py` confirms it stays within budget (~11.9KB free of 512KB). Freshness is tracked per-fetch-cycle, not per-share (see share-feed-design.md's Known Limitations) -- a share the feed silently stops reporting on will still show "LIVE" alongside shares that are genuinely fresh, until a future change adds per-share timestamps if that proves to matter in practice. |
| 2026-08-13 | Extracted shared HTTP response parsing (issue #46) into `http_response.h`/`.cpp` from three independently-converged copies in `home_assistant_manager.cpp`, `air_traffic_manager.cpp`, and `share_price_manager.cpp`, standardizing on Home Assistant's implementation (the most mature: proper case-insensitive header lookup, `Content-Length` parsing, buffered-completion detection) rather than the other two managers' simpler raw-substring versions. `air_traffic_manager.cpp` gained chunked-body dechunking it didn't previously have at all, and both `air_traffic_manager.cpp`/`share_price_manager.cpp` gained early completion detection via `Content-Length` (previously only chunked responses could complete before connection close). New pure functions are host-tested (`tests/host/test_http_response.cpp`). See the new "Network Helper Ownership Boundary" section above for exactly what did and didn't move. | User explicitly chose the fuller-risk option after being asked: a partial extraction (pure functions only, one manager's behavior unchanged) was on the table given all three managers involved have a real lockup incident in their history (#19) and none of this could be verified on real hardware in this session. Discovered mid-implementation that the three managers' HTTP handling wasn't just duplicated but different in quality and in completion-handling *shape* (HA processes inline from the recv callback; the other two defer to `update()` specifically to avoid PCB-reentrancy issues) -- unifying the parsing was judged safe and valuable; unifying the completion-handling architecture was judged out of scope for this pass (see the ownership-boundary doc's reasoning). | JSON body parsing remains duplicated between `air_traffic_manager.cpp` and `share_price_manager.cpp` (`find_object_end`/`extract_bounded_string`/`extract_bounded_number`) -- deliberately deferred, not forgotten; a future pass can fold it into `http_response` or a sibling module if it's still worth doing. The inline-vs-deferred completion-handling question above is unresolved, not just deferred -- worth a dedicated look if HA's manager is ever extended in a way that makes its reentrancy window realistic (e.g. adding an in-flight period/parameter change the way shares has). None of this was exercised against a real HTTP server in this session; verify on hardware before trusting the early-completion behavior change under real network conditions. |
| 2026-08-13 | Split weather/Home Assistant parsing out of `home_assistant_manager.cpp`'s network transport (issue #47) into five pure modules -- `weather_json_scan`, `weather_normalisation`, `weather_forecast_parser`, `home_assistant_weather_parser`, `open_meteo_parser` -- taking the "full split" scope the issue itself proposed rather than a narrower first slice. Notably pulled Home Assistant's current-weather-entity parsing out of `handle_http_status()` (the transport file's lwIP receive callback), where it had been inlined with no dedicated function at all, unlike Open-Meteo's equivalent. New pure functions are host-tested (37 new cases across `tests/host/test_weather_*.cpp`). See the new "Weather Parsing Ownership Boundary" section above. | User was asked whether to take the full issue-scoped split or a safer slice (JSON primitives + Open-Meteo only, leaving the trickier inline-HA-entity extraction for a follow-up issue) given the file's size (3546 lines) and complete lack of existing host coverage; chose the full split. | Once the inline HA-entity block was extracted, ~30 of this file's own wrapper-style functions (kept in #46's extraction as thin pass-throughs) turned out to have no remaining caller in this file and were deleted rather than left as dead code -- a larger diff than #46's "every wrapper stays" precedent, but correct once the only call site moved into a module. `time_manager.cpp` is still not host-buildable (Pico SDK dependency), papered over for this one call site via a host-only stub (see the ownership-boundary section); a real fix would need `time_manager`'s own ISO-datetime-to-local-time conversion extracted into something host-testable, out of scope here. None of this was exercised against a real Home Assistant or Open-Meteo server in this session -- verify on hardware before trusting the extracted HA-entity parsing (previously untested even implicitly, since it lived inline in a callback) against real API responses. |
| YYYY-MM-DD |  |  |  |
