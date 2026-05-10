# MerlinCCU

Merlin Mk1/Mk3 Common Control Unit recreation work using a Raspberry Pi Pico 2 W.

## Merlin Mk1 Background (ASW)

The Royal Navy's Merlin Mk1 (service designation **Merlin HM1**) is the UK ASW
variant of the AgustaWestland **AW101 Merlin**, which traces its origins to the
Anglo-Italian EH101 programme started in the 1980s as a Sea King successor. The
type first flew in 1987 and entered Royal Navy service in the late 1990s, going
on to become a core shipborne helicopter for anti-submarine warfare, surface
surveillance, and general maritime support.

The Royal Navy later pursued the **Merlin HM2** upgrade (often described under
the Merlin capability sustainment programmes), with major mission system and
avionics updates to keep the fleet current for ASW and maritime operations.
This work started well before the first HM2 aircraft were delivered back to the
Fleet Air Arm in the early 2010s, and most HM1 airframes were ultimately
converted to HM2 standard.

This project focuses on a CCU from the **Merlin Mk1/HM1** era. Later **Mk2/HM2**
fits used touchscreen-style control units instead of the older CCU arrangement.

References (for quick orientation):
- Federation of American Scientists (EH101/AW101 overview): https://man.fas.org/dod-101/sys/ac/row/eh101.htm
- Airforce Technology (Merlin HM Mk1 description): https://www.airforce-technology.com/projects/merlin-asw-helicopter/
- AirVectors (HM2 upgrade overview and timeline pointers): https://www.airvectors.net/avaw101.html
- UK MOD (Mk2 delivery announcement): https://www.gov.uk/government/news/royal-navy-receives-upgraded-merlin-helicopters

## What This Repo Is

This project is currently a display and UI bring-up platform for a vintage
Merlin CCU style unit.

The firmware generates the timing needed by a monochrome EL320-class panel
using:

- a 1-bit logical UI framebuffer
- a CPU-built scanout raster
- DMA to stream that raster continuously
- a very small PIO program to shift prepared pin states onto the display lines

The current code is still prototype firmware, but it has moved beyond simple
"can the screen light up?" testing. It now has:

- a cleaner display architecture
- a screensaver mode
- a skeleton input layer ready for future keypad wiring
- a source layout split into smaller logical modules

## Current Status

The firmware currently:

- builds for `pico2_w`
- drives four display output signals from the Pico using PIO
- stores drawing data in a portrait-oriented UI framebuffer
- converts that framebuffer into the panel's native scan order during composition
- uses double buffering for both the UI framebuffer and the DMA scanout raster
- swaps scanout buffers at a frame boundary rather than rewriting the live DMA source
- includes a placeholder keypad/input layer with debouncing logic and GPIO mapping slots
- includes three screen modes:
  - a geometry test pattern
  - a dummy CCU menu screen
  - a Conway's Game of Life screensaver

The Life screensaver is currently the default runtime mode because it is useful
for testing:

- continuous screen updates
- full display refresh behaviour
- timing/performance visibility over USB serial
- visual masking of existing panel burn-in

## Display Validation Workflow

Use this quick workflow for every UI/layout change:

1. Flash from the VS Code Pico extension.
2. Open `http://merlinccu/preview`.
3. Check each page with this pass/fail list:
   - labels, softkey captions, and headings are uppercase
   - data values are mixed case and readable
   - right-edge values are not clipped
   - footer text is visible and not overlapping softkeys
   - status page columns stay aligned across all rows
4. Repeat checks on physical panel and browser preview.
5. Capture one screenshot (preview or panel) for any failed check before fixing.

For a fuller checklist (including weather and integration states), use
`docs/display-test-checklist.md`.

## High-Level Architecture

There are three main graphics layers in the current design:

1. UI framebuffer

This is a compact 1-bit image stored in logical portrait coordinates. All
drawing helpers work in this space, so the UI code does not need to know how
the panel is physically mounted or scanned.

2. Scanout raster

This is a much larger buffer that contains the exact output waveform for a
whole frame. Each packed nibble represents the simultaneous state of:

- `VID`
- `VCLK`
- `HS`
- `VS`

The CPU builds this raster from the UI framebuffer.

3. PIO + DMA scanout

DMA repeatedly feeds the raster into a PIO state machine. The PIO program is
deliberately simple: it just shifts out the already prepared pin states.

## Why There Are Two Coordinate Systems

The display panel does not use the same orientation as the desired UI.

To keep the drawing code simple:

- the UI framebuffer is stored in portrait coordinates
- panel rotation and row offset are handled only during raster composition

This means UI code can draw rectangles, text and lines normally, while the
composition step translates that image into the panel's native electrical scan
order.

## Screensaver Notes

The current screensaver is Conway's Game of Life.

It is implemented on a reduced simulation grid and then scaled up to the full
display. This keeps the CPU and RAM cost reasonable while still creating large,
visible moving patterns on the panel.

Important details:

- the Life field wraps around all four edges
- the simulation automatically reseeds if it becomes static
- the simulation also reseeds if it falls into a short repeating cycle
- USB serial output includes simple timing information for simulation, drawing
  and frame presentation

## Input Layer Status

The code now contains keypad/input support that has moved beyond the original
skeleton stage.

At the moment:

- logical button IDs are defined
- keypad GPIO mappings are assigned
- debounce logic exists
- button polling is wired into the main loop

Key signal GPIOs are now assigned for the keypad matrix work. Remaining
power-line integration is still handled separately from those signal mappings.

## Files In Active Use

- `MerlinCCU.cpp`
  Main entry point and top-level app loop.

- `panel_config.h`
  Shared panel geometry, timing values, and pin base definitions.

- `framebuffer.h` / `framebuffer.cpp`
  UI framebuffer storage and drawing helpers.

- `display.h` / `display.cpp`
  Raster composition, DMA setup, PIO setup, and scanout presentation.

- `input.h` / `input.cpp`
  Placeholder keypad/input layer with debouncing and logical button events.

- `screens.h` / `screens.cpp`
  Demo pattern and dummy menu rendering.

- `screensaver_life.h` / `screensaver_life.cpp`
  Conway's Game of Life state, stepping, reseed logic, and rendering.

- `font_5x7.h`
  Built-in bitmap font used by the framebuffer text helpers.

- `el320_raster.pio`
  The active PIO program. It outputs one 4-bit state at a time onto the four
  display pins.

- `CMakeLists.txt`
  Pico SDK build configuration for the firmware target.

## Signal Mapping

The active firmware expects four contiguous GPIO pins starting at GPIO2:

- `GPIO2` -> `VID`
- `GPIO3` -> `VCLK`
- `GPIO4` -> `HS`
- `GPIO5` -> `VS`

If the hardware wiring changes, update the firmware and any related hardware
notes together.

## Build Notes

This repository uses the Raspberry Pi Pico SDK with CMake.

The exact local build flow depends on your environment. In this project the
Pico VS Code plugin workflow is being used successfully.
CLI builds are supported as long as the Pico environment is exported first
(`PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`, plus PATH entries for toolchain, Ninja,
and CMake). VS Code Pico extension builds remain the default day-to-day flow.

Build reliability note:

- a previous mismatch between generated PIO header symbol styles
  (`kEl320RasterProgram` vs `el320_raster_program`) caused fresh CLI builds to
  fail while cached build folders appeared healthy
- `MerlinCCU.cpp` now resolves both symbol styles via one compatibility helper,
  so clean builds and cached builds behave consistently

## Configuration Files

Local machine- or network-specific settings are intentionally kept out of git.

Use the `.example` files as the starting point:

- `wifi_credentials.example.h` -> `wifi_credentials.h`
  Required for any networked feature.
- `home_assistant_credentials.example.h` -> `home_assistant_credentials.h`
  Required only if you want the REST-based Home Assistant probe and entity
  polling/posting.
- `mqtt_credentials.example.h` -> `mqtt_credentials.h`
  Required only if you want MerlinCCU to appear as a Home Assistant MQTT device.
- `weather_display_config.example.h` -> `weather_display_config.h`
  Optional. Lets the Home page read one Home Assistant `weather.*` entity.

Recommended setup order:

1. Get Wi-Fi working first.
2. Add Home Assistant REST once the device can stay on the network reliably.
3. Add MQTT discovery after the broker is installed and tested in Home Assistant.

There is also a short contributor/setup checklist in `CONTRIBUTING.md`.

## Wi-Fi And Home Assistant Setup

To connect the Pico W to your local network and Home Assistant instance:

- copy `wifi_credentials.example.h` to `wifi_credentials.h`
- set `WIFI_SSID` and `WIFI_PASSWORD`
- copy `home_assistant_credentials.example.h` to `home_assistant_credentials.h`
- set `HOME_ASSISTANT_TOKEN` to a Home Assistant long-lived access token
- set `HOME_ASSISTANT_HOST` to either:
  - a bare host such as `homeassistant.local`
  - a fixed LAN IP such as `192.168.1.20`
  - an `http://` URL such as `http://homeassistant.local:8123`
- optional: copy `mqtt_credentials.example.h` to `mqtt_credentials.h` if you want
  MQTT discovery so Home Assistant sees MerlinCCU as a device
- set `HOME_ASSISTANT_MQTT_HOST` to your broker host or IP
- if your broker requires auth, set `HOME_ASSISTANT_MQTT_USERNAME` and
  `HOME_ASSISTANT_MQTT_PASSWORD`
- optional: copy `weather_display_config.example.h` to `weather_display_config.h`
  if you want the Home page to show weather from Home Assistant
- set `HOME_ASSISTANT_WEATHER_ENTITY_ID` to a real `weather.*` entity such as
  `weather.forecast_home`

Important distinction:

- `HOME_ASSISTANT_TOKEN` is only for the REST API
- `HOME_ASSISTANT_MQTT_USERNAME` and `HOME_ASSISTANT_MQTT_PASSWORD` are broker
  credentials used by Mosquitto or another MQTT broker
- `HOME_ASSISTANT_WEATHER_ENTITY_ID` is just an entity id; it is not a secret

Important limits of the current implementation:

- it probes `GET /api/` over plain HTTP only
- `https://` is not supported yet
- if `.local` name resolution is unreliable on your network, use a fixed IP
- MQTT discovery requires the Home Assistant MQTT integration and a reachable broker
- the UI status page shows Wi-Fi state, IP address, Home Assistant REST state,
  and MQTT discovery state
- the Home page can optionally show current weather and an hourly forecast list
  if a Home Assistant weather entity is configured and supports hourly forecasts

## Keypad Bring-Up Notes

Current keypad work is still provisional. The ribbon-cable breakout and matrix
decode should be treated as bench notes until they are confirmed on hardware.

The local `Keyboard.xlsx` workbook is the current bench-tracing reference for
the remaining front-panel lines, including keypad, backlight LED, and
photoresistor pin investigation. It is kept alongside the source so the raw
bring-up notes are available even where the README only captures the confirmed
results.

From the current spreadsheet in local development, the likely keypad matrix
panel pins are:

- `5, 6, 7, 8, 9, 10, 11`
- `15, 16, 17, 18, 20, 21`

Other spreadsheet notes suggest separate non-key lines for:

- alert/test LEDs
- key backlight power and ground
- a photoresistor pair

Those notes are useful for bring-up, but they are not yet a verified final
netlist.

With the current firmware, the Pico 2 W GPIO situation is:

- `GPIO2..GPIO5` are reserved for the display scanout (`VID`, `VCLK`, `HS`, `VS`)
- likely free for keypad work: `GPIO0`, `GPIO1`, `GPIO6..GPIO22`
- `GPIO26..GPIO28` are also available and can be used as digital inputs/outputs if ADC is not needed there
- do not plan around `RUN`, `3V3_EN`, `VSYS`, `VBUS`, `ADC_VREF`, or ground pins as general GPIO
- do not assume internal wireless-side control lines are available as normal front-panel GPIO

The firmware now includes a keypad diagnostics page that records the last
logical button event seen by the input layer and also shows the current raw
active-line signature from the monitored keypad pins. This is intended as a
bring-up tool while the real matrix decoder is still being worked out.

Use `keypad_matrix_config.example.h` as the starting point for local wiring.
Copy it to `keypad_matrix_config.h`, assign the Pico GPIO connected to each
panel pin you want to observe, and the keypad diagnostics page will show:

- the currently active panel-pin list
- the current active-line bitmask
- how many observed lines are configured and active

Current measured pin accounting from front-panel switch tests:

| Pin | Status | Role | Notes |
| --- | --- | --- | --- |
| 1 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 2 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 3 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 4 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 5 | Used | Matrix drive line | `TEST`, `BRT`, `DIM` when paired with `17`, `16`, `15` |
| 6 | Used | Matrix drive line | Main block row for `LTRS`, `BACK STEP`, arrows, `/`, `CLR` |
| 7 | Used | Matrix drive line | Main block row `A..F`; also `R1` via pin `15` |
| 8 | Used | Matrix drive line | Main block row `G..L`; also `R2` via pin `15` |
| 9 | Used | Matrix drive line | Main block row `M..R`; also `R3` via pin `15` |
| 10 | Used | Matrix drive line | Main block row `S..X`; also `R4` via pin `15` |
| 11 | Used | Matrix drive line | Main block row `Y,Z,T FUNC,.,0,SPC`; also `R5` via pin `15` |
| 12 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 13 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 14 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 15 | Used | Matrix sense line | Right softkey return (`R1..R5`) and `DIM` |
| 16 | Used | Matrix sense line | Key column for `BRT`, `CLR`, `F`, `L`, `R`, `X`, `SPC` |
| 17 | Used | Matrix sense line | Key column for `TEST`, `/`, `E`, `K`, `Q`, `W`, `0` |
| 18 | Used | Matrix sense line | Key column for `Right Arrow`, `D`, `J`, `P`, `V`, `.` |
| 19 | Used | Matrix sense line | Key column for `Left Arrow`, `C`, `I`, `O`, `U`, `T FUNC` |
| 20 | Used | Matrix sense line | Key column for `ALERT`, `BACK STEP`, `B`, `H`, `N`, `T`, `Z` |
| 21 | Used | Matrix sense line | Key column for `LTRS`, `A`, `G`, `M`, `S`, `Y` |
| 22 | Used | Matrix sense line | Left softkey return (`L1..L5`) |
| 23 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 24 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 25 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 26 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 27 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 28 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 29 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |
| 30 | Non-matrix | Unknown non-key line | Not referenced by measured key matrix; candidate for power/LED/photoresistor net pending verification |

Power/ground note:

- The matrix mapping above only proves key-switch connectivity.
- Any non-matrix pin (`1..4`, `12..14`, `23..30`) should be treated as
  unknown until continuity/voltage testing confirms whether it is power,
  ground, LED drive, or photoresistor-related.

Confirmed matrix pattern from current bench tests:

- `5 x 20` = `ALERT`
- `5 x 17,16,15` = `TEST`, `BRT`, `DIM`
- `6 x 21..16` = `LTRS`, `BACK STEP`, `Left Arrow`, `Right Arrow`, `/`, `CLR`
- `7 x 21..16` = `A..F`
- `8 x 21..16` = `G..L`
- `9 x 21..16` = `M..R`
- `10 x 21..16` = `S..X`
- `11 x 21..16` = `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC`
- `7..11 x 22` = `L1..L5`
- `7..11 x 15` = `R1..R5`

Implementation note:

- the observed panel-pin numbering is now confirmed for bring-up and decoding work
- the current GPIO/panel-pin ordering in code is intentionally still a bench-work layout rather than a cleaned-up PCB-oriented netlist
- if a fixed PCB is designed later, it would be reasonable to reorder or rename the pin definitions for clarity, but that cleanup is deferred for now so the confirmed working mapping is preserved

## Wiring Integration Plan

This section separates front-panel lines into two classes:

- direct power rails (do not connect to Pico GPIO)
- logic/sense lines (connect to Pico GPIO, directly or through a driver stage)

### 4-pin display-side connector plan

Use the 4-pin connector for panel power rails only unless bench evidence proves
that one of these lines is a logic-level control input.

- likely direct rails:
  - panel supply rail(s) (for example +V panel power)
  - panel ground return
  - optional backlight/inverter supply rails, if present in your panel variant
- not Pico GPIO:
  - any rail that is continuous to power domains, decoupling capacitors, or
    obvious power conditioning parts on the display assembly

Current confirmed display GPIO signals remain on the scanout connector:

- `GPIO2` -> `VID`
- `GPIO3` -> `VCLK`
- `GPIO4` -> `HS`
- `GPIO5` -> `VS`

### 30-pin keypad/front-panel connector plan

Matrix key lines should be treated as GPIO/sense lines. Dedicated LED power
and other rails should be treated as non-GPIO rails.

- matrix/sense candidates (GPIO domain):
  - confirmed active matrix pins: `5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21, 22`
- likely non-GPIO rails:
  - key backlight power
  - key backlight ground
  - alert/test LED supply return paths if they are current-driving lines
  - photoresistor bias/supply rail(s)

For the photoresistor pair:

- if used as analog sensors, route to ADC-capable pins (`GPIO26..GPIO28`)
- if used as threshold/digital detect only, route through comparator/threshold
  logic or use ADC with firmware thresholding

### Execution order (recommended)

1. Freeze and label the already-confirmed matrix GPIO wiring in your harness.
2. Identify 4-pin connector rails with continuity and voltage checks before
   any Pico connection.
3. Bring up display rail power first, then verify existing scanout signals
   (`VID/VCLK/HS/VS`) still produce stable output.
4. Connect keypad power/backlight rails as power-only nets (not GPIO).
5. Add photoresistor wiring to ADC pins if needed, and log raw ADC values over
   serial for bright/dim transitions.
6. Update `Keyboard.xlsx` and this README with final net names once measured.

### Safety rules for bring-up

- do not attach unknown front-panel lines directly to Pico GPIO until the line
  is verified to be within 0V..3.3V in all panel states
- use series resistors and current limits for first-power tests of unknown LED
  and backlight paths
- common ground between panel/front-panel supplies and Pico is required for
  reliable logic sensing

## Pico Pin Budget (Current)

This is the current practical pin budget based on firmware usage and the bench
layout shown in current bring-up photos.

| Group | Pins | Current use / policy |
| --- | --- | --- |
| Display scanout (fixed) | `GPIO2`, `GPIO3`, `GPIO4`, `GPIO5` | Reserved for `VID`, `VCLK`, `HS`, `VS` (`panel_config.h` `kPinBase=2`) |
| Keypad matrix (active bench set) | `GPIO6..GPIO20` | Used by current matrix monitoring/decoding wiring (`keypad_matrix_config.h` local mapping) |
| Free digital GPIO (observed) | `GPIO21`, `GPIO22` | Available for additional digital lines (for example LED control or spare logic inputs) |
| Free ADC-capable GPIO (observed) | `GPIO26`, `GPIO27`, `GPIO28` | Prefer for photoresistor channels or other analog sensing |
| Network/power infrastructure pins | `VBUS`, `VSYS`, `3V3`, `ADC_VREF`, `RUN`, `GND` | Not general-purpose front-panel GPIO; keep for power, reference, reset, and grounding roles only |

Notes:

- `GPIO26..GPIO28` can be used as digital GPIO if ADC is not needed, but they
  are the best fit for LDR/photoresistor inputs.
- Any reassignment should be mirrored in local `keypad_matrix_config.h` and
  documented here to keep wiring notes aligned with runtime behaviour.

## Deferred Security TODOs

These items are intentionally parked for a later hardening phase so the current
working firmware can be preserved while the network-facing changes are planned
properly.

- add `https://` support in MerlinCCU for the Home Assistant REST client
- enable HTTPS on Home Assistant or on a reverse proxy in front of it
- choose how MerlinCCU will validate the HA server certificate:
  - public CA
  - private/internal CA
  - pinned certificate or public key
- move MQTT to TLS and configure the broker with a certificate MerlinCCU can validate
- create a dedicated Home Assistant user/token for MerlinCCU with only the access it needs
- create a dedicated MQTT account and restrict it to the required topics with broker ACLs
- review whether MerlinCCU should use trusted local DNS and NTP services instead of public defaults
- consider putting MerlinCCU on a trusted SSID or VLAN during and after the migration
- update setup documentation once HTTPS/TLS is actually supported end-to-end

## Roadmap Backlog

This backlog is ordered by delivery dependency so platform work lands before
feature-heavy screens.
Tracking rule: keep each deliverable as a task checkbox. Mark completed items
as `[x]` and append a short completion note such as `(done 2026-05-06, #123)`.

## Phase 1: Platform And Data Foundations

- [ ] replace timed/demo routing with a stable UI state model across all pages
- [ ] finalise keypad matrix decoding and hard/soft key mapping
- [ ] add I2C bus support on spare GPIOs (including any GPIO reallocation needed)
- [ ] add an I2C sensor abstraction for temperature, humidity, air quality, CO2,
  and particulates
- [ ] improve Home Assistant integration so local sensor data is published back to HA
- [ ] define CCU device identity from MAC address with user-facing aliases
  (`CCU#1`, `CCU#2`, etc.)
- [ ] add presence/discovery model for other CCU devices on the network
- [ ] keep module boundaries under review as feature scope grows

## Phase 2: Alerting And Event Workflow

- [x] implement alert state machine and escalation timing (done 2026-05-10, web-preview-first ALRT workflow)
- [x] detect prolonged failures across Wi-Fi, weather fetch, HA REST, and MQTT (done 2026-05-10, retry-threshold driven)
- [ ] add weather-based threshold alerts:
  - high/low temperature triggers (configurable)
  - wind-speed triggers (configurable)
  - evaluate provider-native warnings vs local threshold judgement
- [ ] add sensor-generated alerts (pressure trend, air quality, CO2, particulates)
- [x] implement ALRT-key workflow (done 2026-05-10):
  - ALRT lamp flashes on new alert arrival
  - pressing `ALERT` acknowledges current alert set and opens alert list
  - alert list maps newest alerts to `L1..L5` and `R1..R4` with timestamp labels (`[HH:MM]`)
  - softkey drill-down opens detail page
  - detail actions: `R5=ACCEPT` (clear selected alert), `L5=IGNORE` (return without clearing)
  - cursor keys page the list and scroll detail text
  - note: the web preview ALRT indicator is implemented first, but the real
    ALRT LED still needs dedicated hardware bring-up work:
    - confirm power and return line routing for the ALRT LED channel
    - complete front-panel hardware setup for ALRT LED drive
    - wire firmware lamp output to the physical ALRT LED path once hardware is ready

## Phase 3: Weather UX Expansion

- [ ] keep HA weather feed as baseline while direct providers remain available
- [ ] add weather period softkey cycling:
  - next 9-10 hours
  - day
  - week
- [ ] keep local-time alignment consistent across provider outputs
- [ ] add weather iconography:
  - evaluate 10x10, 12x12, and 16x16 options
  - review practical icon conventions from small-display ecosystems

## Phase 4: New Screen Features

- [ ] add an info screen saver showing:
  - time and date
  - temperature and humidity
  - sunrise/sunset context (show sunset after sunrise, sunrise after sunset)
  - air quality metrics (CO2, particulates, and related sensor values)
- [ ] add share-monitoring feature:
  - [x] add first CCU shares page from Home `L2` with BAE Systems as the initial watched share
  - [x] add share detail page with graph region and period cycling:
    `today`, `week`, `month`, `year`, `all-time`
  - [x] render share detail graph as full-width trend lines with no bounding box
    and show `MIN`/`MAX` values beneath the graph
  - [x] choose no-account market data source:
    Yahoo Finance chart JSON (`query1.finance.yahoo.com/v8/finance/chart/<symbol>`)
    is the current target because it returns both current price metadata and
    history for the graph periods without an API key
  - [ ] choose symbols in config
  - [ ] add/remove watched shares from UI or web config
  - [x] fetch live BAE Systems market price and history from the selected provider
  - [x] refresh live share data on a five-minute cadence while shares pages are active
  - [ ] show live current price with up/down/flat indicator beside each softkey label

## Phase 5: Observability, Reliability, And Ops

- [ ] add access monitoring for CCU activity (presence, timestamps, features used)
- [ ] publish access/presence telemetry to HA for processing
- [ ] add explicit diagnostics for malformed responses and unsupported payloads
- [ ] tune backoff/retry policy and optional request rate limiting
- [ ] define stale-data display policy during outages
- [ ] monitor Synology backup/sync health to Proxmox-hosted Synology VM and surface
  sync anomalies

## Phase 6: Display Quality R&D (Greyscale And AA)

- [ ] investigate deterministic temporal modulation for perceived greyscale
  (target 2-4 effective levels)
- [ ] keep panel timing fixed (no HSYNC/VSYNC/pixel-clock changes) and vary pixel
  duty across frame cycles
- [ ] test coverage-aware glyph/icon assets (edge coverage levels rather than pure
  1-bit glyphs)
- [ ] evaluate mixed spatial + temporal anti-aliasing for diagonals and curves
- [ ] expose user mode selection:
  - `Blocky` (current crisp 1-bit rendering)
  - `Smoothed` (temporal/coverage-based rendering)
- [ ] validate flicker/shimmer thresholds and CPU/raster budget impact before
  promoting beyond experimental mode

## Next Implementation Candidates

The most practical next slice is:

- [x] land Phase 2 alert state model skeleton with ALRT navigation and action flow (done 2026-05-10)
- [ ] extend Phase 3 weather period controls (`hour`, `day`, `week`) and layout
      handling
- [ ] add weather icon pipeline and placeholder icon set (10x10 and 16x16 trial)

