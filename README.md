# MerlinCCU

Merlin Mk1/Mk3 Common Control Unit recreation work using a Raspberry Pi Pico 2 W.

## What This Repo Is

This project is currently focused on display bring-up.

The firmware generates the video timing needed by a monochrome EL320-class panel
using:

- a 1-bit logical UI framebuffer
- a CPU-built scanout raster
- DMA to stream that raster continuously
- a tiny PIO program to shift the prepared pin states onto the display signals

At the moment the code is a working prototype and test bed, not a full finished
CCU implementation.

## Current Status

The firmware currently:

- builds for `pico2_w`
- drives four output signals from the Pico using PIO
- stores drawing data in a portrait-oriented UI framebuffer
- converts that framebuffer into the panel's native scan order during composition
- uses double buffering for both the UI framebuffer and the DMA scanout raster
- swaps scanout buffers at a frame boundary rather than rewriting the live DMA source
- alternates between a simple geometry test screen and a dummy menu screen

The firmware does not yet include:

- real CCU application logic
- button or keypad input handling
- communications with other hardware or services
- a completed hardware abstraction layer

## High-Level Architecture

There are three main data layers in the current design:

1. UI framebuffer

This is a compact 1-bit image stored in logical portrait coordinates. All
drawing helpers work in this space, so menu layout code does not need to know
how the panel is physically mounted.

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

## Files In Use

- `MerlinCCU.cpp`
  The main firmware source. It contains the panel configuration, drawing
  helpers, raster generation, DMA/PIO scanout setup, and demo screens.

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

## Suggested Next Steps

The display pipeline is now in a much better state architecturally, so this is
a reasonable point to commit and push as one logical packet.

Likely future work:

- document the hardware and panel timing more formally
- add input handling
- replace the demo UI with real CCU screens
- separate the firmware into smaller source files as the project grows
