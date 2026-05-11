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
- Menu/state controller for Home, Weather, Status, Settings, diagnostics, alerts, and shares.
- Runtime configuration persisted in flash with CRC/version handling.
- Local web configuration page with admin-password option.
- Browser display preview at `/preview`, including framebuffer mirror, lamp preview, and virtual key input.
- Wi-Fi station mode with DHCP/static-IP support, NetBIOS hostname, NTP/SNTP, and simple internet reachability probing.
- Home Assistant REST client for status/entity/weather flows.
- Open-Meteo direct weather source using configured latitude/longitude.
- Hour, day, and week weather period views.
- MQTT discovery/state publishing for Home Assistant diagnostic sensors.
- Alert workflow with alert list/detail pages, acknowledgement/clear actions, and generated system alerts.
- Share page and share detail graph for BAE Systems using Yahoo Finance chart data.
- Selectable screen savers: Life, Clock, Starfield, Matrix, Radar, Rain, Worms, and Random.
- Provisional keypad matrix monitor/decoder for confirmed softkeys, navigation keys, and numeric keys.

Still incomplete or provisional:

- Physical ALRT/TEST LED drive, key backlight, panel backlight, and photoresistor wiring are not final.
- Some printed front-panel keys are shown in the web preview but are not wired into firmware behaviour yet.
- Watched share symbols are not user-configurable from the UI or web config yet.
- Weather iconography and warning/threshold alert rules are still planned.
- I2C sensor support is planned but not implemented.
- TLS certificate validation is not complete. HTTPS/TLS client paths exist, but trust-store and certificate validation policy still need hardening.
- Multicore separation is not implemented yet.

## Runtime Surfaces

The firmware currently exposes these operator/developer surfaces:

- Physical display output through the EL320 scanout path.
- Physical/provisional keypad input via `input.cpp` and `keypad_matrix_config.h`.
- Local web configuration at `/config` or `/` when remote configuration is enabled.
- Local browser preview at `/preview`.
- Framebuffer snapshot endpoints at `/api/framebuffer` and `/api/framebuffer.pbm`.
- Browser key/lamp preview endpoints under `/api/button` and `/api/panel-state`.
- MQTT discovery/state topics when the MQTT manager is configured and enabled.
- USB serial diagnostics for boot, network, fetch, and selected timing events.

## High-Level Architecture

The firmware is deliberately split into small managers rather than one large application file.

Primary runtime flow:

- `MerlinCCU.cpp` owns startup, the main loop, screen saver activation, and display present decisions.
- `console_controller.cpp` owns UI state, softkey routing, alert state, and runtime config application.
- `screens.cpp` draws the current menu/page into the back framebuffer.
- `display.cpp` converts the UI framebuffer into the native panel raster and hands it to DMA.
- `input.cpp` polls physical/provisional keypad wiring and emits logical button events.
- `web_config_server.cpp` handles local HTTP configuration, preview, and virtual key input.
- `wifi_manager.cpp`, `home_assistant_manager.cpp`, `mqtt_manager.cpp`, and `share_price_manager.cpp` advance network-facing state machines.

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
- The preview deliberately de-prioritises framebuffer/lamp polling while key presses are queued so the single-session Pico HTTP server does not make web keys feel sluggish.
- Ghost keys preserve visual panel fidelity but report that they are not wired in firmware yet.

## Display Validation Workflow

Use this workflow for UI/layout changes:

1. Flash from the VS Code Pico extension, or perform one bounded CLI build/flash pass.
2. Open `http://merlinccu/preview`.
3. Check each page for uppercase labels/headings, readable mixed-case values, unclipped right-edge text, footer spacing, and aligned status rows.
4. Repeat checks on the physical panel and browser preview.
5. Capture a preview or panel screenshot for failed checks before fixing.

For a fuller checklist, use `docs/display-test-checklist.md`.

## Build Notes

This repository uses the Raspberry Pi Pico SDK with CMake.

The Pico VS Code extension workflow is the normal day-to-day path. CLI builds are also supported, but configure the same tool paths first:

- `PICO_SDK_PATH`
- `PICO_TOOLCHAIN_PATH`
- `PATH` entries for the ARM toolchain, Ninja, and CMake

Keep CLI checks bounded to one configure/build pass so failures return the first actionable error.

Known build compatibility note:

- `MerlinCCU.cpp` supports both newer and older generated PIO header symbol names (`kEl320RasterProgram` and `el320_raster_program`). This keeps clean and cached builds consistent across Pico SDK/pioasm combinations.

## Configuration

Runtime settings are primarily persisted in Pico flash and can be edited from the local web configuration page when remote configuration is enabled.

Local machine- or network-specific fallback headers are intentionally kept out of git. Use the `.example` files as starting points:

- `wifi_credentials.example.h` -> `wifi_credentials.h`
- `home_assistant_credentials.example.h` -> `home_assistant_credentials.h`
- `mqtt_credentials.example.h` -> `mqtt_credentials.h`
- `weather_display_config.example.h` -> `weather_display_config.h`
- `keypad_matrix_config.example.h` -> `keypad_matrix_config.h`

Recommended setup order:

1. Get Wi-Fi connected first.
2. Enable the local web config page and move editable settings into flash.
3. Add Home Assistant REST once Wi-Fi is stable.
4. Add MQTT discovery after the broker is reachable from the Pico.
5. Add direct weather coordinates if using Open-Meteo.
6. Configure keypad matrix pins only after the front-panel line is verified as safe for Pico GPIO.

Important limits:

- Home Assistant endpoints may be configured as host, `http://host[:port]`, or `https://host[:port]`.
- HTTPS currently uses the Pico TLS path without a completed CA/trust-store validation design.
- Open-Meteo currently uses HTTP on port 80.
- MQTT is currently plain TCP MQTT, not MQTT over TLS.
- If `.local` name resolution is unreliable, use a fixed IP.

## Active Source Map

Core firmware:

- `MerlinCCU.cpp`: startup, main loop, mode switching, and frame presentation.
- `panel_config.h`: panel geometry, timing, and fixed scanout pin base.
- `framebuffer.h` / `framebuffer.cpp`: 1-bit UI framebuffer and drawing helpers.
- `display.h` / `display.cpp`: native raster composition, DMA, PIO setup, and display presentation.
- `console_model.h` / `console_model.cpp`: UI state types and default state.
- `console_controller.h` / `console_controller.cpp`: menu routing, softkeys, alerts, runtime config sync, and UI state changes.
- `screens.h` / `screens.cpp`: page rendering.
- `input.h` / `input.cpp`: keypad polling, matrix probing, debounced button events, and diagnostics.

Network/config managers:

- `config_manager.h` / `config_manager.cpp`: flash-backed runtime configuration.
- `wifi_manager.h` / `wifi_manager.cpp`: Wi-Fi, DHCP/static IP, NetBIOS, SNTP, and reachability state.
- `home_assistant_manager.h` / `home_assistant_manager.cpp`: Home Assistant and direct weather requests.
- `mqtt_manager.h` / `mqtt_manager.cpp`: Home Assistant MQTT discovery/state publishing.
- `share_price_manager.h` / `share_price_manager.cpp`: Yahoo Finance share data fetch and parsing.
- `web_config_server.h` / `web_config_server.cpp`: local HTTP configuration and preview server.

Screen savers:

- `screensaver_life.*`
- `screensaver_clock.*`
- `screensaver_starfield.*`
- `screensaver_matrix.*`
- `screensaver_radar.*`
- `screensaver_rain.*`
- `screensaver_worms.*`

## Signal Mapping

The active display firmware expects four contiguous GPIO pins starting at GPIO2:

| Pico GPIO | Signal |
| --- | --- |
| `GPIO2` | `VID` |
| `GPIO3` | `VCLK` |
| `GPIO4` | `HS` |
| `GPIO5` | `VS` |

If display wiring changes, update `panel_config.h` and this README together.

## Keypad Bring-Up Status

The keypad matrix is no longer just a placeholder, but it remains a bench bring-up configuration rather than a final PCB/netlist.

Current firmware support:

- Logical button IDs exist for the ten softkeys, navigation keys, clear/back, and numeric keys.
- Matrix closure definitions exist for currently supported front-panel keys.
- `keypad_matrix_config.h` maps observed panel pins to Pico GPIOs locally.
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
| Keypad matrix bench set | Local `keypad_matrix_config.h` mapping | Used for current matrix monitoring/decoding wiring |
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
6. Update `Keyboard.xlsx`, `keypad_matrix_config.h`, and this README after final net names are confirmed.

Safety rules:

- Use series resistors and current limits for first-power tests of unknown LED and backlight paths.
- Common ground between panel/front-panel supplies and Pico is required for reliable logic sensing.
- Do not treat any non-matrix pin as GPIO-safe until it has been measured.

## Security And Hardening TODOs

Network support is currently development-friendly rather than hardened.

Deferred hardening work:

- Add certificate validation for HTTPS Home Assistant requests.
- Move MQTT to TLS and configure broker certificates.
- Create a dedicated Home Assistant user/token with only the access MerlinCCU needs.
- Create a dedicated MQTT account and restrict it to required topics with broker ACLs.
- Decide whether MerlinCCU should use trusted local DNS and NTP rather than public defaults.
- Consider placing MerlinCCU on a trusted SSID or VLAN during and after TLS migration.
- Add rate limiting and clearer malformed-response diagnostics for externally sourced payloads.

## Roadmap

Platform and architecture:

- [x] Display scanout through PIO/DMA.
- [x] Menu state model and contextual softkey map.
- [x] Flash-backed runtime configuration.
- [x] Local web configuration and display preview.
- [ ] Define and implement a safe multicore split for UI and background processing.
- [ ] Add I2C bus support on spare GPIOs.
- [ ] Add sensor abstractions for temperature, humidity, air quality, CO2, and particulates.
- [ ] Keep module boundaries under review as feature scope grows.

Input and front-panel hardware:

- [x] Provisional keypad matrix monitor and decoder.
- [x] Keypad diagnostics page.
- [ ] Finalise physical keypad harness/netlist.
- [ ] Bring up ALRT/TEST LED hardware drive.
- [ ] Bring up key and panel backlight control.
- [ ] Bring up photoresistor or brightness sensing if retained.

Weather and integrations:

- [x] Home Assistant REST status/entity/weather flow.
- [x] Open-Meteo direct weather source.
- [x] Hour/day/week weather views.
- [x] MQTT discovery for core diagnostic sensors.
- [ ] Add weather icon pipeline and trial small icon sets.
- [ ] Add weather threshold alerts for temperature, wind, and provider warnings.
- [ ] Publish local sensor data back to Home Assistant once sensors exist.

Alerting and operations:

- [x] Alert state machine and escalation workflow.
- [x] System alerts for Wi-Fi, time sync, Home Assistant, weather, MQTT, keypad, display lag placeholder, and share data failures.
- [x] ALRT-key workflow in web preview and UI state.
- [ ] Wire the real ALRT lamp path once hardware is ready.
- [ ] Add access/presence telemetry if multiple CCUs are used.
- [ ] Define stale-data display policy during outages.

Shares and data display:

- [x] Shares landing page and BAE Systems initial watch row.
- [x] Share detail page with period cycling and trend graph.
- [x] Yahoo Finance chart fetch and parser for current price/history.
- [x] Five-minute refresh while shares pages are active.
- [ ] Make watched shares configurable.
- [ ] Show live up/down/flat indicators beside share softkey labels.

Display quality R&D:

- [ ] Investigate deterministic temporal modulation for perceived greyscale.
- [ ] Test coverage-aware glyph/icon assets.
- [ ] Evaluate mixed spatial and temporal anti-aliasing.
- [ ] Expose user mode selection between crisp 1-bit and smoothed rendering if the panel tolerates it.
- [ ] Validate flicker, shimmer, CPU cost, and raster budget before promoting beyond experiment.

## Next Practical Candidates

Short-term useful slices:

1. Finalise physical ALRT/TEST LED and backlight wiring assumptions.
2. Make watched shares configurable from web config.
3. Add weather icons after settling the small bitmap asset size.
4. Define the multicore event/message boundary before moving any work to core 1.
5. Add I2C sensor scaffolding once spare GPIO allocation is confirmed.
