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

## Share/Air-Traffic Parser Ownership Boundary

Issue #72 extracted the two remaining un-host-tested response parsers --
`share_price_manager.cpp`'s local shares-feed parsing (issue #42's
`{"shares":[...]}` contract) and `air_traffic_manager.cpp`'s adsb.lol/
dump1090-family `{"ac":[...]}` parsing -- following the same pattern as #46
(HTTP framing) and #47 (weather/HA parsing):

- **`bounded_json.h`/`.cpp`** -- the bounded-range JSON scanning primitives
  (`find_bounded`, `extract_bounded_string`, `extract_bounded_number`,
  `bounded_value_is_string`, `find_object_end`) both managers had
  independently converged on, now shared. They had quietly diverged on one
  point: `share_price_manager.cpp`'s `extract_bounded_number` explicitly
  rejected a quoted value, while `air_traffic_manager.cpp`'s version had no
  such guard and relied on `strtod` failing on the opening `"` instead --
  same practical result, different code path. Consolidated on the explicit
  guard (the more defensive of the two).
- **`share_feed_parser.h`/`.cpp`** -- `share_feed::parse_shares_feed_response()`,
  operating on an explicit `std::array<ShareWatchEntry, kMaxWatchedShares>&`
  and share count instead of `share_price_manager.cpp`'s file-static
  `g_status`. The 256-entry history-downsampling scratch buffer
  (`g_history_parse_values`) moved from a static global into a local
  variable in the process, since nothing needed it to persist between calls
  -- a small incidental static-RAM win (~512 bytes).
- **`air_traffic_response_parser.h`/`.cpp`** -- `air_traffic_response::
  parse_aircraft_candidates()`, returning the closest-N `Candidate` array by
  distance. Deliberately stops at candidate selection: per-aircraft
  snail-trail history merging (`merge_trail_history()`) and display-text
  formatting stay in `air_traffic_manager.cpp`, since they're not response
  parsing -- `merge_trail_history()` in particular reads and mutates
  `g_tracked`, a manager-persistent state store with no equivalent on the
  parsing side.

Neither manager's HTTP-completion/transport code moved -- same reasoning as
#46/#47's ownership boundary. New pure functions are host-tested in
`tests/host/test_bounded_json.cpp`, `test_share_feed_parser.cpp`, and
`test_air_traffic_response_parser.cpp`, each including a realistic
full-response fixture (matching `docs/share-feed-design.md` and
`docs/adsb-air-traffic-feature-design.md`'s documented contracts) plus
malformed/edge-case fixtures (missing required field, quoted-vs-numeric
mismatches, a truncated final object, an empty array), per issue #72's
acceptance criteria. The weather/HA and HTTP-framing parsers already had
this kind of coverage from #46/#47 and weren't revisited.

## Golden-Image Rendering Tests

Issue #71 made `screens.cpp` and its eight page-family files
(`status_screens.cpp`, `weather_screens.cpp`, `calendar_screens.cpp`,
`shares_screens.cpp`, `pinter_screens.cpp`, `settings_screens.cpp`,
`alert_screens.cpp`, `air_traffic_screens.cpp`) host-testable, and added
pixel-snapshot regression tests so a layout bug (a softkey label overlapping
content, a margin collision) fails a test run instead of needing a human to
spot it on a screenshot -- the project's actual bug history per the issue.

**What was blocking host-compilation, and how each was resolved:**

- **`program_flash_bytes()`/`static_ram_bytes()`/`total_ram_bytes()`**
  (`status_screens.cpp`, reading `hardware/flash.h`'s `PICO_FLASH_SIZE_BYTES`
  and linker symbols `__flash_binary_start/end`, `__end__`, `__StackLimit`
  directly during render) -- moved into a new `ImageFootprintStatus` field on
  `ConsoleState`, computed exactly once in `make_default_console_state()`
  (`console_model.cpp`, not itself host-compiled). Unlike `HeapStatus`/
  `StackStatus`, these figures are fixed at link time, so a single
  construction-time read is correct, not a periodic re-sample.
- **`display::present_skipped_count()`** (`status_screens.cpp`, and
  transitively `display.h`'s `hardware/pio.h`) -- added as a third field on
  the existing `DisplayTimingStatus` (which already periodically samples
  `frame_count()`/`last_rebuild_us()` in `MerlinCCU.cpp`), so
  `status_screens.cpp` no longer includes `display.h` at all.
- **`to_ms_since_boot(get_absolute_time())`** (5 call sites across
  `status_screens.cpp`, `weather_screens.cpp`, `shares_screens.cpp`,
  `air_traffic_screens.cpp`, each computing "now" for data-freshness text) --
  threaded as an explicit `now_ms` parameter from `screens::draw_menu_screen()`
  down through each page family's public entry point, matching
  `build_data_freshness_text()`'s own existing `now_ms` parameter. The one
  real caller (`MerlinCCU.cpp`) now passes `to_ms_since_boot(get_absolute_time())`
  at the call site instead of each renderer reading it independently -- and
  golden tests get a deterministic, caller-chosen timestamp instead of real
  wall-clock time, which pixel-snapshot tests need to be reproducible at all.
- **`PICO_ERROR_NONE`** (`screens.cpp`, one comparison in the Local
  Conditions air-quality metric) -- replaced with a locally named
  `kNoErrorCode = 0`; that constant is stable across the Pico SDK, so this
  isn't a behaviour change, just removing a header dependency for one literal.
- **`uint` in `panel_config.h`** (`kPinBase`, needing `pico/stdlib.h` purely
  for that typedef) -- changed to `unsigned int` (the same type). This let
  `panel_config.h` -- and therefore `kUiWidth`/`kUiHeight`/`kUiStride`/
  `kUiFbSize`, which every rendering file depends on -- drop its Pico SDK
  include entirely.
- **`config_manager::settings()`** (called by `screen_banners.cpp`, which
  every page renders through, plus two Status subpages) and
  **`environment_sensor_manager::air_quality_band_text()`** (a pure
  score-to-label mapping embedded in the I2C-driving sensor manager, linked
  in via `screens.cpp`'s Local Conditions renderer even though no golden test
  exercises that page) -- both have real implementations that pull in
  `hardware/*`/`pico/*` headers with no small pure slice worth extracting for
  this issue alone, so both got host-only stub implementations in
  `tests/host/stubs/`, matching the `time_manager_stub.cpp` precedent from
  issue #47. The `config_manager` stub returns a default-constructed
  `RuntimeConfig`; the `environment_sensor_manager` stub is a byte-for-byte
  copy of the real banding thresholds/labels (kept in sync by hand, not a
  fake, since it's small and rarely changes).
- **A stray `#include "display.h"`** in `calendar_screens.cpp` (unused --
  left over from issue #45's split) and **`screens.cpp`'s own unused
  `#include "display.h"`** were both just deleted.

**The golden-image harness itself** (`tests/host/golden_test_support.h/.cpp`,
`tests/host/test_golden_screens.cpp`): a golden is a `P4` (binary) PBM file
under `tests/host/golden/<name>.pbm` -- the UI framebuffer's own row-major,
MSB-first 1bpp packing (`panel_config.h`'s `kUiStride`) is already a valid
PBM bitplane, so a golden file is just that raw buffer with a `P4\n<width>
<height>\n` text header prepended, making it directly openable in any
PBM-aware image viewer. `golden_test::check_golden(name, fb)` compares
byte-for-byte; on mismatch it reports the first differing pixel's (x, y) and
writes the actual render alongside the golden as `<name>.actual.pbm`
(git-ignored) for inspection. **Regenerating goldens after a deliberate
layout change** is one command: `MERLINCCU_REGENERATE_GOLDEN=1` (any
non-empty value) in the environment before running `host_tests` rewrites
every golden the run touches -- inspect the diff before committing it, same
as any other generated-file review.

Covers the issue's named pages (Calendar, Calendar Detail, Status Resources,
Share Detail, Settings, Keypad Debug) -- 6 `HOST_TEST` cases, each building a
minimal but deterministic `ConsoleState` fixture and rendering through the
real `screens::draw_menu_screen()` dispatch, not a shortcut. Verified the
harness actually catches a regression (not just a pass-through) by
temporarily shifting `shares_screens.cpp`'s detail-row `start_y` by 16px --
it failed at the exact expected pixel, then was reverted before committing.

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
| 2026-08-13 | Added fixture-based regression tests for the share-price and air-traffic response parsers (issue #72), extracting `share_price_manager.cpp`'s shares-feed parsing and `air_traffic_manager.cpp`'s aircraft-list parsing into pure modules (`share_feed_parser`, `air_traffic_response_parser`) first, since neither was host-testable as-is -- same situation `home_assistant_manager.cpp` was in before #46/#47. Also consolidated the two managers' independently-converged bounded-JSON helper set into one shared `bounded_json` module, fixing a small behavioral divergence found in the process (`extract_bounded_number`'s quoted-value handling). 24 new host tests, each provider with a realistic full-response fixture plus malformed/edge-case ones, closing the last gap #72's acceptance criteria named. See the new "Share/Air-Traffic Parser Ownership Boundary" section above. | User chose the full scope (extract + consolidate + fixture-test both providers) over a shares-only slice or a tests-only pass, given both managers needed the same #46/#47-style extraction before fixture tests could mean anything, and the duplicated-helper divergence was worth fixing once found rather than working around. | Static RAM dropped ~512 bytes and flash ~1.8KB versus the pre-#72 baseline -- a genuine (if small) improvement from de-duplicating the bounded-JSON helpers and turning a 512-byte scratch global into a local variable, not measurement noise (verified via full clean rebuild). None of this was exercised against a real adsb.lol feed or local HA shares endpoint in this session -- the fixtures are built from the documented contracts (`docs/share-feed-design.md`, `docs/adsb-air-traffic-feature-design.md`), not captured real traffic. |
| 2026-08-13 | Fixed a two-part proportions regression in the web Display Preview page (`web_config_server.cpp`'s `build_preview_page()`) left over from issue #79's pixel-for-pixel canvas fix. Part one: `--rect-key-w`/`--rect-key-h`/`--font-small`/`--font-large` (top-row and A-Z keypad matrix buttons) were never scaled down when the display panel shrank ~25% (358x450 -> 270x338) to render at native resolution, so the keys read as oversized; `.display-shell` also had no `justify-content`, so the now-narrower display+softkey column left-aligned within `.display-bay` while the (justify-content:center) top-row/keypad stayed centered, reading as the whole preview shifted left -- fixed by scaling the four CSS variables by the same ~0.75 factor (78/56/12/18px -> 59/42/9/14px) and adding `justify-content:center` to `.display-shell`. Part two, found from a follow-up screenshot after part one shipped: with the keypad/display now genuinely smaller, the outer `.ccu`/`.ccu-fixed` panel (still hardcoded to the original 560px) and the `.display-bay`/`.keybed` background boxes (both stretching to fill their block-level parent's full width by default) left a large empty margin around the shrunk content -- fixed by shrinking `.ccu`/`.ccu-fixed` to 472px (keeping the same ~18px slack around the widest row, `.display-shell` at 426px, that the original 560px/514px pairing had) and giving `.display-bay`/`.keybed` `width:fit-content;margin:...auto 0` so they hug their content instead of stretching. | User reported the regression with a screenshot after this session's #72 work merged, then reported a second, related regression ("now the ccu is too wide") from a follow-up screenshot once part one was pushed -- both root-caused via reading the actual CSS box model rather than guesswork, tracing the underlying cause to the same commit `02eb726` (#79) leaving several sibling elements unscaled/unstretched when it shrank the display panel. | Not visually verified in a browser this session -- no way to run the Pico's actual HTTP server or a dev-server equivalent for this embedded page locally; both fixes are CSS-only, worked out by hand from the exact box-model math and verified by clean firmware rebuilds only. Ask the user to confirm the preview looks right before considering issue #79 fully closed -- a third round of adjustment wouldn't be surprising given the first two both missed a sibling element. |
| 2026-08-13 | Split the Calendar page family (shared calendar overview, event-detail page) out of `screens.cpp` into `calendar_screens.cpp`, continuing issue #45's staged split (Status: 2026-07-08, Weather: 2026-07-09). Owner-label lookup, weekday/relative-day text formatting, and the footer arrow drawing all moved together since they're Calendar-only; the shared `screens_shared.h` primitives (`DetailRow`/`draw_compact_detail_rows`/`draw_centered_text`) stayed put and are called through the `screens::` qualifier, matching the Status/Weather precedent. `screens.cpp` dropped from 2195 to 1950 lines with no rendering change (verified via clean firmware rebuild). | Continues the staged `screens.cpp` split rather than attempting Shares/Pinter/Settings/Alerts in the same pass -- picked Calendar as the next candidate since it was fully self-contained (no helpers shared with any other still-in-file page family, unlike Shares' graph-plotting helpers which Local Conditions Graph also needs). | Shares, Pinter, Settings, and Alerts remain in `screens.cpp`. Settings in particular bundles 11 `MenuPage` sub-pages (Device/Wifi/HomeAssistant/Mqtt/AirTraffic/ScreenSaver/WeatherSources/TimeZone/Alignment/KeypadDebug/GreyscaleTest) behind mostly-stub bodies and is a worse first pick than the others -- likely worth its own scoped follow-up rather than one more "move everything" pass. Local Conditions/Local Conditions Graph also still live in `screens.cpp`, sharing graph-plotting helpers with the not-yet-split Shares detail page; whichever of the two moves first will need to promote those helpers into `screens_shared.h`. |
| 2026-08-13 | Finished issue #45's staged `screens.cpp` split: Pinter, Shares, Settings, and Alert pages moved into `pinter_screens.cpp`, `shares_screens.cpp`, `settings_screens.cpp`, and `alert_screens.cpp` respectively, following the same pattern as the earlier Status/Weather/Calendar splits. Two helper groups needed promoting into `screens_shared.h` first since code on both sides of a split needed them: the graph-plotting primitives (`GraphPlotArea`/`draw_graph_plot_border`/`graph_plot_x`/`graph_plot_y`, needed by both the moved Shares history graph and the still-in-file Local Conditions history graph) and the softkey-bracket-drawing primitive (`draw_softkey_selection_brackets`, needed by both the core softkey-label renderer that stays in `screens.cpp` and Settings' screen-saver timeout scratchpad that moved out). `screens.cpp`: 1950 -> 1455 lines (3319 -> 1455 across the whole staged effort since 2026-07-08, a 56% reduction). No rendering change -- pure code motion, verified via full clean firmware rebuild. Closes #45. | User explicitly asked to keep splitting until the issue could be closed, after the Calendar-only PR left it open. Settings had been flagged as the worst next candidate (11 mostly-stub sub-pages) in the prior entry, but turned out mechanically straightforward once actually attempted -- the placeholder bodies meant most of its bulk was `(void)fb; (void)console_state;` pairs, not real logic to untangle. | Local Conditions and Local Conditions Graph remain in `screens.cpp` -- deliberately, since the issue's own scope list never named them, and they share the newly-promoted graph-plotting helpers with Shares rather than needing their own split. `screens.cpp` is now down to: shared/exposed primitives, `menu_page_title`, the Home page, `draw_demo_screen`/`draw_calibration_screen` (non-menu diagnostic screens), and `draw_menu_screen`'s dispatch -- a reasonable long-term shape per the issue's acceptance criteria ("reduced to shared helpers and dispatch, or otherwise materially smaller"). |
| 2026-08-13 | Added golden-image (pixel snapshot) regression tests for `screens.cpp` rendering (issue #71). Made the whole render layer host-testable first: moved `program_flash_bytes`/`static_ram_bytes`/`total_ram_bytes` (previously read live from linker symbols during every Resources-page render) into a new `ImageFootprintStatus` field on `ConsoleState`, sampled once at construction; added `present_skipped_count` to the existing `DisplayTimingStatus`; threaded an explicit `now_ms` parameter through `screens::draw_menu_screen()` and the 5 call sites that previously called `to_ms_since_boot(get_absolute_time())` directly; dropped `panel_config.h`'s `pico/stdlib.h` dependency (one `uint` typedef, changed to `unsigned int`); stubbed `config_manager::settings()` and `environment_sensor_manager::air_quality_band_text()` for the host build (both have real implementations with real hardware dependencies not worth extracting for this issue alone). Added `tests/host/golden_test_support.h/.cpp` (PBM-based goldens, byte-compare with pixel-coordinate diagnostics on mismatch, `MERLINCCU_REGENERATE_GOLDEN=1` to regenerate) and `test_golden_screens.cpp` covering Calendar, Calendar Detail, Status Resources, Share Detail, Settings, and Keypad Debug. Bumped `tests/host/`'s C++ standard to 20 (MSVC has no GNU-extensions mode, and the firmware's `-std=gnu++17` accepts designated initializers -- several `ConsoleState`-adjacent structs use them -- that strict C++17 doesn't). See the new "Golden-Image Rendering Tests" section above for the full list of what was blocking host-compilation and how each was resolved. | User asked to look closer at the exact blocking call sites before choosing between a clean refactor (inject dependencies, sample linker-symbol reads once) and host-only stub headers; the closer look found more blockers than the issue's own text described (3 linker-symbol-reading functions, not 1, plus `present_skipped_count`, `config_manager::settings()`, and a stray unused `#include "display.h"` inherited from issue #45's split) -- proceeded with the clean-refactor approach throughout except where the real implementation was itself hardware-bound (`config_manager`, `environment_sensor_manager`), which got stubs matching the issue #47 precedent instead. | Verified the harness catches real regressions, not just passes trivially, by deliberately shifting a coordinate in `shares_screens.cpp` and confirming the golden test failed at the exact expected pixel before reverting. Local Conditions/Local Conditions Graph and the remaining ~19 renderer functions have no golden coverage yet -- the issue's acceptance criteria named 4 pages as the minimum ("at least"), not full coverage; extending to more pages is straightforward now that the host-compilation blockers are cleared, just needs fixture data per page. None of this was run against real hardware -- the goldens are pixel-exact captures of *this session's* render output, not independently verified against a physical panel photograph. |
| 2026-08-13 | Started issue #44's staged `console_controller.cpp` split: extracted the Pinter workflow (brew catalogue metadata, dock/fridge capacity accounting, softkey label construction, start/advance/reset state machine, flash-save deferral) into `pinter_controller.h`/`.cpp`. Every extracted function takes `ConsoleState&`/`const ConsoleState&` explicitly rather than reaching for a hidden global, matching the #45 display-split pattern. The generic softkey-label scratch-buffer helpers (`build_selection_softkey_label`, `build_uppercase_title`, plus a new `dynamic_softkey_label_buffer` accessor) moved from the file's anonymous namespace into an exposed `console_controller::console_controller_internal` namespace (declared in a new `console_controller_internal.h`, not part of the public `console_controller.h` API) so `pinter_controller.cpp` can share them; the rest of `console_controller.cpp` keeps using them unqualified via a `using` declaration. `console_controller.cpp`: 4817 -> 4290 lines (11% reduction), no behavioural change (verified via clean firmware rebuild, all 206 host tests, and identical `.bss`/`.data` size against a stashed pre-change baseline build). | 2026-07-08's Decision Log entry flagged this file's remaining candidates (softkey construction, alert sync, Pinter, calendar, settings routing, status ingestion) as too entangled via a 463-reference global for a small staged step. A closer read this session found the entanglement worse than that entry described -- `update_softkeys_from_state()`/`apply_softkey_route()` are 800+/370+-line dispatchers every candidate module needs to call back into, and there is no golden-image-style regression coverage for controller logic the way #71 gave the display layer. User was offered a staged-first-cut vs. full-one-PR choice with that risk stated; chose staged, picking Pinter as the cleanest-bounded candidate to prove the explicit-`ConsoleState&`-parameter pattern actually works here before committing to the rest. | Alert synchronisation, calendar filtering/navigation, settings route handling, and softkey construction/dispatch remain in `console_controller.cpp`. Alert sync (`sync_system_alerts()`, ~275 lines) looks like the next-cleanest candidate on the same read-only-state-plus-own-counters shape Pinter had; softkey construction/dispatch (`update_softkeys_from_state()`/`apply_softkey_route()`) should stay last, as the top-level orchestrator every other split calls into, mirroring how `screens.cpp` kept `draw_menu_screen()` after #45. |
| 2026-08-13 | Continued issue #44's staged `console_controller.cpp` split: extracted alert state synchronisation (the `AlertCode` enum, failure-sample counters, active-alert queue mutation, `sync_system_alerts()`, and the alert list/detail-page open flows) into `alert_controller.h`/`.cpp`, following the same `ConsoleState&`-parameter pattern as the Pinter extraction earlier this session. `AlertCode` itself stayed a private implementation detail of `alert_controller.cpp` -- the one place `console_controller.cpp` needs to touch a specific alert's suppression state (the `AlertAccept` route) does so through a new `suppress_alert_code(uint8_t)` taking the raw `ActiveAlert::code` index, not the enum. A new `annunciation_summary()` accessor was added (not a mechanical move) so `update_lamps_from_state()` can read the annunciation summary for lamp state without `console_controller.cpp` needing direct access to the now-private `g_alert_acknowledged_sequence` counter. `console_controller.cpp`: 4290 -> 3615 lines (4817 -> 3615 across both staged steps this session, 25% reduction). No behavioural change intended (verified via clean firmware rebuild, all 206 host tests, and unchanged flash/RAM growth pattern -- +168 bytes flash, RAM unchanged). | Alert sync was identified as the next-cleanest candidate in the prior entry: like Pinter, it reads broadly from `ConsoleState` but writes back only to its own queue/counters, with no dependency on the still-in-file softkey dispatcher beyond the shared label-buffer primitives already exposed via `console_controller_internal`. | Calendar filtering/navigation, settings route handling, and softkey construction/dispatch (`update_softkeys_from_state()`/`apply_softkey_route()`) remain in `console_controller.cpp`. Softkey dispatch should still stay last, as the top-level orchestrator every split module is called from -- extracting it before the others would mean threading `ConsoleState&` through a function that calls back into every other module, the reverse of the dependency direction this staged approach has relied on so far. |
| 2026-08-13 | Finished issue #44's staged `console_controller.cpp` split: extracted calendar owner filtering/day navigation into `calendar_controller.h`/`.cpp`, then settings route handling, softkey-caption formatting, weather/time-zone/screen-saver static definition tables, and the screen-saver timeout scratchpad into `settings_controller.h`/`.cpp` (bundling in a few small state toggles -- share-slot selection, air-traffic view mode/paging -- that shared its shape but fit no other module). `console_controller.cpp`: 3615 -> 3448 -> 2508 lines across these two steps (4817 -> 2508 total this session, a 48% reduction). Core keypad text-entry primitives (`letter_mode_text`, `button_digit_value`, `alpha_character_from_button`, `text_character_from_button`) stayed in `console_controller.cpp` rather than moving to `settings_controller.cpp`, since they're used broadly by `handle_button_event`'s general text entry, not just the screen-saver timeout scratchpad -- `button_digit_value` is reachable from `settings_controller.cpp` via a new `console_controller_internal::keypad_digit_value()` accessor. Closes #44: the file now holds what the #45 display-split precedent left in `screens.cpp` -- shared primitives, status ingestion, and the `update_softkeys_from_state()`/`apply_softkey_route()` dispatch every extracted module is called from -- deliberately left as the dispatcher rather than split further, matching how `screens.cpp` kept `draw_menu_screen()`. No behavioural change intended across any of the four staged commits (verified via clean firmware rebuild after each step, 206/206 host tests, and flash/RAM tracked against a stashed pre-session baseline build throughout -- final numbers flat to slightly improved, not regressed). | Continued the staged approach from the prior two entries once the Pinter and alert extractions proved the `ConsoleState&`-parameter pattern held up cleanly against this file's heavier entanglement. Calendar was next-cleanest (small, self-contained, like Pinter/alert); Settings was tackled last and largest since it was the true "everything else" bucket the issue's own candidate list named, and the interleaving with core input-handling primitives (letter mode, digit/character decoding) needed to be read carefully rather than assumed extractable. | The staged, multi-PR path this issue took (rather than one large rewrite) is itself worth noting for future large-file splits: reconnaissance underestimated the entanglement twice (this file's dispatcher size, then the settings/core-input interleaving) and course-correcting after each staged PR caught both before they became a problem, versus discovering them mid-way through a single unreviewable diff. `update_softkeys_from_state()` and `apply_softkey_route()` remain 800+/370+ line dispatch functions with no golden-image-style regression coverage the way `screens.cpp` has since #71 -- a natural next step if this area sees heavy churn, though not required by #44's own acceptance criteria. |
| YYYY-MM-DD |  |  |  |
