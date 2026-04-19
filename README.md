# MerlinCCU

Merlin Mk1/Mk3 Common Control Unit recreation work using a Raspberry Pi Pico 2 W.

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
- full display refresh behavior
- timing/performance visibility over USB serial
- visual masking of existing panel burn-in

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

The code now contains a small input skeleton for future keypad support.

At the moment:

- logical button IDs are defined
- placeholder GPIO mappings are present
- debounce logic exists
- button polling is wired into the main loop

The actual GPIO assignments are still left as `-1` until the keypad wiring is
ready. That means the input layer is safe to leave in place even before the
hardware is connected.

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

## Other Files In The Repo

Some other `.pio` files are present from earlier experiments or alternate ideas.
At the moment, `el320_raster.pio` is the one used by the build.

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
Do not run `cmake` or `cmake --build` from the CLI on this setup; use the VS
Code Pico extension for build/flash actions.

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

| Pin | Status | Notes |
| --- | --- | --- |
| 1 | Unused | Not referenced by any measured key |
| 2 | Unused | Not referenced by any measured key |
| 3 | Unused | Not referenced by any measured key |
| 4 | Unused | Not referenced by any measured key |
| 5 | Used | `TEST`, `BRT`, `DIM` |
| 6 | Used | `LTRS`, `BACK STEP`, `Left Arrow`, `Right Arrow`, `/`, `CLR` |
| 7 | Used | `R1`, `A`, `B`, `C`, `D`, `E`, `F` |
| 8 | Used | `R2`, `G`, `H`, `I`, `J`, `K`, `L` |
| 9 | Used | `R3`, `M`, `N`, `O`, `P`, `Q`, `R` |
| 10 | Used | `R4`, `S`, `T`, `U`, `V`, `W`, `X` |
| 11 | Used | `R5`, `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC` |
| 12 | Unused | Not referenced by any measured key |
| 13 | Unused | Not referenced by any measured key |
| 14 | Candidate | Likely missing left-key column for `L1..L5`, possibly `ALERT` |
| 15 | Used | `DIM`, `R1`, `R2`, `R3`, `R4`, `R5` |
| 16 | Used | `BRT`, `CLR`, `F`, `L`, `R`, `X`, `SPC` |
| 17 | Used | `TEST`, `/`, `E`, `K`, `Q`, `W`, `0` |
| 18 | Used | `Right Arrow`, `D`, `J`, `P`, `V`, `.` |
| 19 | Used | `Left Arrow`, `C`, `I`, `O`, `U`, `T FUNC` |
| 20 | Used | `BACK STEP`, `B`, `H`, `N`, `T`, `Z` |
| 21 | Used | `LTRS`, `A`, `G`, `M`, `S`, `Y` |
| 22 | Unused | Not referenced by any measured key |
| 23 | Unused | Not referenced by any measured key |
| 24 | Unused | Not referenced by any measured key |
| 25 | Unused | Not referenced by any measured key |
| 26 | Unused | Not referenced by any measured key |
| 27 | Unused | Not referenced by any measured key |
| 28 | Unused | Not referenced by any measured key |
| 29 | Unused | Not referenced by any measured key |
| 30 | Unused | Not referenced by any measured key |

Confirmed matrix pattern from current bench tests:

- `6 x 21..16` = `LTRS`, `BACK STEP`, `Left Arrow`, `Right Arrow`, `/`, `CLR`
- `7 x 21..16` = `A..F`
- `8 x 21..16` = `G..L`
- `9 x 21..16` = `M..R`
- `10 x 21..16` = `S..X`
- `11 x 21..16` = `Y`, `Z`, `T FUNC`, `.`, `0`, `SPC`
- `7..11 x 15` = `R1..R5`
- `5 x 17,16,15` = `TEST`, `BRT`, `DIM`

Unresolved keys from the current tests:

- `ALERT`
- `L1`
- `L2`
- `L3`
- `L4`
- `L5`

Current working hypothesis:

- panel pin `14` is the strongest candidate for the missing left-key column
- expected mapping if that hypothesis is correct:
  - `L1..L5` = `7..11 x 14`
  - `ALERT` = likely `5 x 14`
- this is still unconfirmed and should be treated as a hypothesis, not a solved netlist

Pending verification:

- ask Sam to confirm whether panel pin `14` is active on a known-good CCU
- if confirmed, retest `L1..L5` and `ALERT` with particular attention to drive/sense activity on `14`

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

## Deferred Functional TODOs

These are non-security items that are worth keeping visible for future passes.

- connect the real keypad wiring and assign GPIOs
- replace the timed/demo screen mode selection with a real UI state model
- replace the provisional keypad diagnostics page with a real matrix decoder and key map
- confirm the spreadsheet matrix against real continuity and switch-press tests
- decide how the hard keys and side softkeys should be represented in the input model
- add a future system menu with screen saver settings
- turn the placeholder weather source page into a real source-selection workflow
- support more than one weather provider or feed behind the weather UI
- improve the weather page layout as more data columns and status lines are added
- decide which Home page values should degrade gracefully when HA data is missing
- add a clearer user-facing distinction between Wi-Fi loss, HA loss, and missing weather data
- review whether the 5-minute HA refresh cadence is the right tradeoff for responsiveness and network load
- add more useful HA-backed entities if they fit the CCU concept cleanly
- keep refining the module boundaries as more real CCU features are added

## Deferred UI And Interaction TODOs

- replace placeholder screens and routes with a coherent navigation model
- define the softkey behavior consistently across all screens
- decide which controls belong on the Home page versus secondary pages
- add any missing iconography needed for status and fault conditions
- review whether the current text layout still works once more live data is added
- document font choices, spacing rules, and banner conventions so future UI changes stay consistent
- decide whether the screensaver should be configurable from the UI

## Deferred Hardware And Platform TODOs

- document the hardware wiring and panel timing in more detail
- validate the keypad electrical design before final GPIO assignments are locked in
- decide whether any display timing values should move from compile-time constants to documented tuning parameters
- review memory headroom as more UI and network features are added
- review CPU time spent in raster composition, weather rendering, and network parsing as the firmware grows

## Deferred Reliability TODOs

- add a clearer strategy for backoff and retry across Wi-Fi, HA REST, and MQTT
- decide whether stale HA data should be cached and shown for longer during outages
- add more explicit diagnostics for malformed HA responses and unsupported payloads
- review whether any optional requests should be rate-limited or skipped after repeated failures
- check whether forecast payload size limits are still appropriate as more providers are tested
- add a documented test checklist for network loss, HA restarts, broker loss, and recovery behavior

## Likely Next Steps

If work resumes soon, the most obvious next items are:

- connect the real keypad wiring and assign GPIOs
- replace the timed/demo screen mode selection with a real UI state model
- turn the placeholder weather source page into a real source-selection workflow
- document the hardware and panel timing in more detail
