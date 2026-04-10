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

## Likely Next Steps

Reasonable future work from here:

- connect the real keypad wiring and assign GPIOs
- replace the timed/demo screen mode selection with a real UI state model
- add a future system menu with screen saver settings
- document the hardware and panel timing in more detail
- continue refining the new module boundaries as more real CCU features are added
