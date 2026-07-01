# Architecture

## Why This Exists

Capture the stable firmware architecture so future changes do not rely on
README history, chat context, or incidental source-code discovery.

## What Belongs Here

- Runtime ownership boundaries between core, display, input, network, config,
  and sensor modules.
- Data-flow rules for `ConsoleState`, framebuffer ownership, manager snapshots,
  and persistent configuration.
- Constraints specific to Raspberry Pi Pico, Pico SDK, lwIP/CYW43, flash writes,
  PIO, DMA, and optional future multicore work.
- Architecture decisions that affect how new features should be integrated.

## Current Runtime Shape

- `src/core/MerlinCCU.cpp`: startup, main loop, screen saver decisions, display
  present scheduling.
- `src/core/console_controller.cpp`: UI state, softkey routing, alert state,
  runtime config application.
- `src/display/screens.cpp`: page rendering into the logical framebuffer.
- `src/display/display.cpp`: framebuffer-to-panel raster composition and DMA/PIO
  presentation.
- `src/core/input.cpp`: physical/provisional keypad polling and logical button
  events.
- `src/network/*`: Wi-Fi, HTTP preview/config, Home Assistant, MQTT, time, and
  share-price state machines.
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

## Multicore Notes

- Current firmware is single application core.
- Do not move work to core 1 until ownership, stack use, flash-write safety,
  and lwIP/CYW43 callback rules are reviewed.
- Safe early candidates are sensor polling, compensation maths, payload parsing,
  and data reduction.

## Decision Log

| Date | Decision | Reason | Follow-up |
| --- | --- | --- | --- |
| YYYY-MM-DD |  |  |  |

## Open Questions

- 
