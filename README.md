# MerlinCCU

Merlin Mk1/HM1 Common Control Unit recreation firmware for a Raspberry Pi Pico 2 W.

Status: active firmware bring-up, not a finished product. The project now has working display scanout, menu/UI state, local web configuration, browser preview control, Wi-Fi integration, weather data, Home Assistant/MQTT integration, alert workflow, share-price display, and provisional keypad matrix decoding.

## Merlin Mk1 Background

The Royal Navy Merlin Mk1, service designation Merlin HM1, is the UK anti-submarine warfare variant of the AgustaWestland AW101 Merlin. It traces back to the Anglo-Italian EH101 programme and the Sea King replacement effort. This project focuses on the older Mk1/HM1-era Common Control Unit rather than the later HM2 touchscreen-style control units.

Reference material for quick orientation:

- Federation of American Scientists EH101/AW101 overview: https://man.fas.org/dod-101/sys/ac/row/eh101.htm
- Airforce Technology Merlin HM Mk1 overview: https://www.airforce-technology.com/projects/merlin-asw-helicopter/
- AirVectors AW101 and HM2 upgrade overview: https://www.airvectors.net/avaw101.html
- UK MOD Merlin Mk2 delivery announcement: https://www.gov.uk/government/news/royal-navy-receives-upgraded-merlin-helicopters

## Current Firmware Snapshot

Working now:

- Pico 2 W target using C++17 and the Pico SDK.
- EL320-style monochrome display scanout through PIO and DMA.
- Portrait 1-bit UI framebuffer with double buffering.
- CPU-built scanout raster with frame-boundary buffer swaps.
- Menu/state controller for Home, Calendar, Weather, Status, Settings, diagnostics, alerts, and shares.
- Runtime configuration persisted in flash with CRC/version handling.
- Local web configuration page with admin-password option.
- Browser display preview at `/preview`, including framebuffer mirror, lamp preview, and virtual key input.
- Wi-Fi station mode with DHCP/static-IP support, NetBIOS hostname, NTP/SNTP, and simple internet reachability probing.
- Home Assistant REST client for status/entity/weather flows.
- Open-Meteo direct weather source using configured latitude/longitude.
- Hourly, Next 24 Hours, and Next 7 Days weather period views.
- MQTT discovery/state publishing for Home Assistant diagnostic sensors.
- Alert workflow with alert list/detail pages, acknowledgement/clear actions, and generated system alerts.
- Calendar UI scaffold with neutral owner filtering, relative-day footer, and
  softkey event-detail pages.
- Share page and share detail graph with explicit demo data while the
  Home Assistant/local share feed is pending.
- Selectable screen savers: Life, Clock, Starfield, Matrix, Radar, Rain, Worms, and Random.
- Provisional keypad matrix monitor/decoder for all confirmed front-panel keys.

Still incomplete or provisional:

- Physical ALRT/TEST LED drive, key backlight, panel backlight, and photoresistor wiring are not final.
- Text-entry consumers are still limited, but the confirmed front-panel keys now generate firmware events.
- Live Home Assistant calendar ingestion is not implemented yet.
- Real calendar participant labels belong in local `config/calendar_identities.h`,
  which is ignored by git. Tracked demo calendar data uses synthetic examples.
- Watched share symbols are not user-configurable from the UI or web config yet.
- Weather iconography and warning/threshold alert rules are still planned.
- Disabled I2C sensor-discovery scaffolding exists for the optional Waveshare
  Pico Environment Sensor board, including BME280/BME680 chip-ID probing; full
  sensor reads are not implemented yet.
- TLS certificate validation is not complete. HTTPS/TLS client paths exist, but trust-store and certificate validation policy still need hardening.
- Multicore separation is not implemented yet.

## Runtime Surfaces

The firmware currently exposes these operator/developer surfaces:

- Physical display output through the EL320 scanout path.
- Physical/provisional keypad input via `src/core/input.cpp` and
  `config/keypad_matrix_config.h`.
- Local web configuration at `/config` or `/` when remote configuration is enabled.
- Local browser preview at `/preview`.
- Framebuffer snapshot endpoints at `/api/framebuffer` and `/api/framebuffer.pbm`.
- Browser key/lamp preview endpoints under `/api/button` and `/api/panel-state`.
- MQTT discovery/state topics when the MQTT manager is configured and enabled.
- USB serial diagnostics for boot, network, fetch, and selected timing events.

## High-Level Architecture

The firmware is deliberately split into small managers rather than one large application file.

Primary runtime flow:

- `src/core/MerlinCCU.cpp` owns startup, the main loop, screen saver
  activation, and display present decisions.
- `src/core/console_controller.cpp` owns UI state, softkey routing, alert
  state, and runtime config application.
- `src/display/screens.cpp` draws the current menu/page into the back
  framebuffer.
- `src/display/display.cpp` converts the UI framebuffer into the native panel
  raster and hands it to DMA.
- `src/core/input.cpp` polls physical/provisional keypad wiring and emits
  logical button events.
- `src/network/web_config_server.cpp` handles local HTTP configuration,
  preview, and virtual key input.
- `src/network/wifi_manager.cpp`, `src/network/home_assistant_manager.cpp`,
  `src/network/mqtt_manager.cpp`, and `src/network/share_price_manager.cpp`
  advance network-facing state machines.
- `src/sensors/environment_sensor_manager.cpp` owns optional I2C environment
  board discovery while full sensor drivers are still pending.

The important ownership rule is that UI state is changed through `console_controller`, and rendered frames are only presented after drawing a complete back buffer. This avoids partially updated menu frames becoming visible.

## Core Allocation Direction

Current firmware runs on one application core. That is intentional until there is a measured bottleneck and a clean ownership boundary.

Preferred future direction:

- One core should own UI-facing work: physical input, web key application, `ConsoleState`, menu routing, framebuffer ownership, and display presentation.
- The other core can later handle background work: network request scheduling, response parsing, sensor polling, data reduction, and other non-UI processing.
- Cross-core communication should use small queues or mailboxes containing immutable snapshots or compact events.
- The background core must not mutate `ConsoleState`, framebuffer pointers, or display state directly.
- Moving lwIP/CYW43 network traffic across cores needs explicit locking and testing. Payload decoding is a safer first candidate than moving raw Wi-Fi stack ownership.

This split is a design target, not current behaviour.

## Display Path

There are three graphics layers:

1. UI framebuffer

The UI framebuffer is a compact 1-bit image stored in logical portrait coordinates. Drawing helpers work in this space so UI code does not know how the panel is mounted or scanned.

2. Scanout raster

The scanout raster contains the exact output waveform for a full native display frame. Each packed nibble represents the simultaneous state of `VID`, `VCLK`, `HS`, and `VS`.

3. PIO and DMA scanout

DMA repeatedly feeds the raster into a PIO state machine. The PIO program is intentionally simple: it shifts prepared pin states onto the display lines.

The panel orientation differs from the desired UI orientation, so rotation and row-offset handling live in the composition step rather than in every screen renderer.

## Web Preview Notes

The preview is a development harness, not a production UI. It mirrors the framebuffer and provides a virtual keypad so the CCU can be driven from a laptop.

Current behaviour:

- `/preview` renders the display, softkeys, mapped hard keys, ALRT/TEST lamp indicators, and a pop-out mode.
- Mapped browser keys POST to `/api/button` as logical `ButtonId` events.
- `LTRS` cycles the front-panel text mode between `ABC`, `123`, and `abc`; the same indicator is shown in the top display banner.
- The preview deliberately de-prioritises framebuffer/lamp polling while key presses are queued so the single-session Pico HTTP server does not make web keys feel sluggish.

## Display Validation Workflow

Use this workflow for UI/layout changes:

1. Flash from the VS Code Pico extension, or perform one bounded CLI build/flash pass.
2. Open `http://merlinccu/preview`.
3. Check each page for uppercase labels/headings, readable mixed-case values, unclipped right-edge text, footer spacing, and aligned status rows.
4. Repeat checks on the physical panel and browser preview.
5. Capture a preview or panel screenshot for failed checks before fixing.

For a fuller checklist, use `docs/display-test-checklist.md`.

Greyscale rendering feasibility (temporal/spatial dithering, panel timing
limits) is written up in `docs/greyscale-investigation.md`, tracking issue
#27.

## Build Notes

This repository uses the Raspberry Pi Pico SDK with CMake.

The Pico VS Code extension workflow is the normal day-to-day path. CLI builds are also supported, but configure the same tool paths first:

- `PICO_SDK_PATH`
- `PICO_TOOLCHAIN_PATH`
- `PATH` entries for the ARM toolchain, Ninja, and CMake

Keep CLI checks bounded to one configure/build pass so failures return the first actionable error.

For the repo-wide build, style, and validation rules, see [docs/development.md](docs/development.md).

Known build compatibility note:

- `src/core/MerlinCCU.cpp` supports both newer and older generated PIO header
  symbol names (`kEl320RasterProgram` and `el320_raster_program`). This keeps
  clean and cached builds consistent across Pico SDK/pioasm combinations.

### Host Tests

`tests/host/` is a separate, non-cross-compiling CMake project covering
keypad matrix decode logic and alert ordering/acknowledgement rules with the
machine's native compiler -- no Pico SDK or ARM toolchain required (issue
#14). Run it independently of the firmware build:

```
cd tests/host
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Configuration

Runtime settings are primarily persisted in Pico flash and can be edited from the local web configuration page when remote configuration is enabled.

Local machine- or network-specific fallback headers are intentionally kept out of git. Use the `.example` files as starting points:

- `config/wifi_credentials.example.h` -> `config/wifi_credentials.h`
- `config/home_assistant_credentials.example.h` ->
  `config/home_assistant_credentials.h`
- `config/mqtt_credentials.example.h` -> `config/mqtt_credentials.h`
- `config/weather_display_config.example.h` ->
  `config/weather_display_config.h`
- `config/keypad_matrix_config.example.h` -> `config/keypad_matrix_config.h`
- `config/environment_sensor_config.example.h` ->
  `config/environment_sensor_config.h`

Recommended setup order:

1. Get Wi-Fi connected first.
2. Enable the local web config page and move editable settings into flash.

`config/wifi_credentials.h` can define an ordered `kLocalWifiCredentials`
array. The firmware tries each access point in definition order, then pauses
briefly before starting the list again. If a Wi-Fi SSID is saved from the web
config page, that runtime network is tried after the compile-time list as a
fallback. If an AP associates but repeated internet probes fail, the firmware
leaves that AP and moves to the next configured credential.
3. Add Home Assistant REST once Wi-Fi is stable.
4. Add MQTT discovery after the broker is reachable from the Pico.
5. Add direct weather coordinates if using Open-Meteo.
6. Configure keypad matrix pins only after the front-panel line is verified as safe for Pico GPIO.
7. Enable the optional Waveshare environment sensor board only after confirming
   its I2C pins do not clash with the keypad harness.

Important limits:

- Home Assistant endpoints may be configured as host, `http://host[:port]`, or `https://host[:port]`.
- HTTPS currently uses the Pico TLS path without a completed CA/trust-store validation design.
- Open-Meteo currently uses HTTP on port 80.
- MQTT is currently plain TCP MQTT, not MQTT over TLS.
- If `.local` name resolution is unreliable, use a fixed IP.

## Active Source Map

Core firmware:

- `src/core/MerlinCCU.cpp`: startup, main loop, mode switching, and frame presentation.
- `include/config/panel_config.h`: panel geometry, timing, and fixed scanout pin base.
- `include/display/framebuffer.h` / `src/display/framebuffer.cpp`: 1-bit UI framebuffer and drawing helpers.
- `include/display/display.h` / `src/display/display.cpp`: native raster composition, DMA, PIO setup, and display presentation.
- `include/core/console_model.h` / `src/core/console_model.cpp`: UI state types and default state.
- `include/core/console_controller.h` / `src/core/console_controller.cpp`: menu routing, softkeys, alerts, runtime config sync, and UI state changes.
- `include/display/screens.h` / `src/display/screens.cpp`: page rendering.
- `include/core/input.h` / `src/core/input.cpp`: keypad polling, matrix probing, debounced button events, and diagnostics.

Network/config managers:

- `include/config/config_manager.h` / `src/config/config_manager.cpp`: flash-backed runtime configuration.
- `include/network/wifi_manager.h` / `src/network/wifi_manager.cpp`: Wi-Fi, DHCP/static IP, NetBIOS, SNTP, and reachability state.
- `include/network/home_assistant_manager.h` / `src/network/home_assistant_manager.cpp`: Home Assistant and direct weather requests.
- `include/network/mqtt_manager.h` / `src/network/mqtt_manager.cpp`: Home Assistant MQTT discovery/state publishing.
- `include/network/share_price_manager.h` /
  `src/network/share_price_manager.cpp`: disabled direct-provider share fetch
  scaffold pending the Home Assistant/local feed.
- `include/network/web_config_server.h` / `src/network/web_config_server.cpp`: local HTTP configuration and preview server.

Sensor managers:

- `include/sensors/i2c_bus.h` / `src/sensors/i2c_bus.cpp`: small Pico SDK I2C
  wrapper for static-lifetime sensor drivers.
- `include/sensors/i2c_register_device.h` / `src/sensors/i2c_register_device.cpp`:
  bounded register transactions for simple I2C devices.
- `include/sensors/bme_environmental_sensor.h` /
  `src/sensors/bme_environmental_sensor.cpp`: BME280/BME680 ID probe scaffold.
- `include/sensors/environment_sensor_manager.h` /
  `src/sensors/environment_sensor_manager.cpp`: disabled-by-default Waveshare
  board discovery scaffold.

Screen savers:

- `include/display/screensavers/` and `src/display/screensavers/`

## Signal Mapping

The active display firmware expects four contiguous GPIO pins starting at GPIO2:

| Pico GPIO | Signal |
| --- | --- |
| `GPIO2` | `VID` |
| `GPIO3` | `VCLK` |
| `GPIO4` | `HS` |
| `GPIO5` | `VS` |

If display wiring changes, update `include/config/panel_config.h` and this
README together.

## Keypad Bring-Up Status

The keypad matrix is no longer just a placeholder, but it remains a bench bring-up configuration rather than a final PCB/netlist.

Current firmware support:

- Logical button IDs exist for all confirmed front-panel matrix keys, including the `A..Z` block, punctuation, `LTRS`, `ALERT`, `TEST`, `BRT`, and `DIM`.
- Matrix closure definitions exist for all confirmed front-panel keys.
- `config/keypad_matrix_config.h` maps observed panel pins to Pico GPIOs locally.
- The diagnostics page shows active panel pins, active masks, probe drive pins, hit masks, and decoded key legends.
- Alerts can detect suspicious keypad states such as `MULTI` or too many active lines.

`Keyboard.xlsx` remains the raw bench-tracing reference for unresolved front-panel lines, including keypad, LEDs, backlight, and photoresistor investigation.

Confirmed matrix pattern from current bench tests:

| Closure | Key group |
| --- | --- |
| `5 x 20` | `ALERT` |
| `5 x 17`, `5 x 16`, `5 x 15` | `TEST`, `BRT`, `DIM` |
| `6 x 21..16` | `LTRS`, `BACK STEP`, left arrow, right arrow, `/`, `CLR` |
| `7 x 21..16` | `A..F` |
| `8 x 21..16` | `G..L` |
| `9 x 21..16` | `M..R` |
| `10 x 21..16` | `S..X` |
| `11 x 21..16` | `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC` |
| `7..11 x 22` | `L1..L5` |
| `7..11 x 15` | `R1..R5` |

Current measured 30-pin front-panel switch map:

| Pin | Status | Role | Notes |
| --- | --- | --- | --- |
| 1 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 2 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 3 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 4 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 5 | Used | Matrix drive line | `ALERT`, `TEST`, `BRT`, `DIM` group |
| 6 | Used | Matrix drive line | Main navigation row |
| 7 | Used | Matrix drive line | Main block row `A..F`; also `R1` via pin `15` |
| 8 | Used | Matrix drive line | Main block row `G..L`; also `R2` via pin `15` |
| 9 | Used | Matrix drive line | Main block row `M..R`; also `R3` via pin `15` |
| 10 | Used | Matrix drive line | Main block row `S..X`; also `R4` via pin `15` |
| 11 | Used | Matrix drive line | Main block row `Y,Z,T FUNC,.,0,SPC`; also `R5` via pin `15` |
| 12 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 13 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 14 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 15 | Used | Matrix sense line | Right softkey return and `DIM` |
| 16 | Used | Matrix sense line | Key column for `BRT`, `CLR`, `F`, `L`, `R`, `X`, `SPC` |
| 17 | Used | Matrix sense line | Key column for `TEST`, `/`, `E`, `K`, `Q`, `W`, `0` |
| 18 | Used | Matrix sense line | Key column for right arrow, `D`, `J`, `P`, `V`, `.` |
| 19 | Used | Matrix sense line | Key column for left arrow, `C`, `I`, `O`, `U`, `T FUNC` |
| 20 | Used | Matrix sense line | Key column for `ALERT`, `BACK STEP`, `B`, `H`, `N`, `T`, `Z` |
| 21 | Used | Matrix sense line | Key column for `LTRS`, `A`, `G`, `M`, `S`, `Y` |
| 22 | Used | Matrix sense line | Left softkey return |
| 23 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 24 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 25 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 26 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 27 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 28 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 29 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |
| 30 | Non-matrix | Unknown non-key line | Candidate for power, LED, or photoresistor net pending verification |

Power/ground note:

- The matrix mapping only proves key-switch connectivity.
- Unknown non-matrix pins must be continuity and voltage tested before any Pico GPIO connection.
- Treat key backlight, panel backlight, LED supply, LED return, and photoresistor lines as separate hardware investigations.

## Pico Pin Budget

Current practical pin budget:

| Group | Pins | Current use / policy |
| --- | --- | --- |
| Display scanout | `GPIO2`, `GPIO3`, `GPIO4`, `GPIO5` | Reserved for `VID`, `VCLK`, `HS`, `VS` |
| Keypad matrix bench set | Local `config/keypad_matrix_config.h` mapping | Used for current matrix monitoring/decoding wiring |
| Optional environment I2C | Local `config/environment_sensor_config.h` mapping | Disabled by default; confirm it does not clash with keypad GPIOs before enabling |
| Spare digital GPIO | Depends on local keypad mapping | Candidate for LED control or spare logic inputs |
| ADC-capable GPIO | `GPIO26`, `GPIO27`, `GPIO28` | Prefer for photoresistor or other analogue sensing |
| Infrastructure pins | `VBUS`, `VSYS`, `3V3`, `ADC_VREF`, `RUN`, `GND` | Not general-purpose front-panel GPIO |

Do not attach unknown front-panel lines directly to Pico GPIO until the line is verified to remain within 0 V..3.3 V in all panel states.

## Hardware Integration Plan

Recommended order:

1. Freeze and label the already-confirmed matrix wiring in the harness.
2. Identify display/front-panel power rails with continuity and voltage checks.
3. Bring up display rail power first, then verify `VID`, `VCLK`, `HS`, and `VS` still produce stable output.
4. Connect keypad/backlight/LED rails as power or driven-output nets only after current paths are understood.
5. Route photoresistor lines to ADC-capable pins if analogue readings are required.
6. Update `Keyboard.xlsx`, `config/keypad_matrix_config.h`, and this README after final net names are confirmed.

Safety rules:

- Use series resistors and current limits for first-power tests of unknown LED and backlight paths.
- Common ground between panel/front-panel supplies and Pico is required for reliable logic sensing.
- Do not treat any non-matrix pin as GPIO-safe until it has been measured.

## Security And Hardening

Network support is currently development-friendly rather than hardened. The
remaining hardening work is tracked in GitHub Issues rather than in this file.

The local web configuration page (`/config`) has no login of any kind -- any
device on the local network can view and change settings. A "Remote Access"
toggle on that page (`remote_config_enabled`) controls whether the web server
runs at all; turn it off if you only want setup changes to come from the
front panel. Flash contents (including saved Wi-Fi/MQTT/Home Assistant
credentials) are stored as plaintext, so treat a device with physical or
debug-port access as having those credentials exposed.

## Open Work

Active development work is tracked in GitHub Issues. Current themes are:

- hardware bring-up and pinout finalisation
- environment sensor and multicore preparation
- alert and lamp hardware completion
- calendar and weather integration follow-up
- shares configurability and display polish
- validation, regression, and host-test coverage

Use the issue tracker for detailed acceptance criteria and progress notes.
