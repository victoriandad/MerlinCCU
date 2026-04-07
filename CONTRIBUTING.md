# MerlinCCU Contributing Notes

This repo is still hardware bring-up firmware, so small disciplined changes are
better than broad refactors.

## Setup Checklist

1. Copy `wifi_credentials.example.h` to `wifi_credentials.h` and fill in your
   Wi-Fi settings.
2. If you want Home Assistant REST support, copy
   `home_assistant_credentials.example.h` to
   `home_assistant_credentials.h`.
3. If you want Home Assistant MQTT discovery, copy
   `mqtt_credentials.example.h` to `mqtt_credentials.h`.
4. Keep those local credential files out of version control.

## Working Rules

- Branch from `main` for each feature or cleanup.
- Keep network secrets, tokens, broker passwords and local IP choices in the
  ignored local headers, never in tracked files.
- Prefer updating public headers and example files when behavior changes.
- Keep comments focused on intent, data flow, or hardware-specific behavior.
- Avoid large formatting-only churn.

## Validation Expectations

- Firmware changes should be verified on hardware before merging.
- USB serial logs are often the fastest way to diagnose timing, Wi-Fi, Home
  Assistant, and MQTT issues.
- Status-page changes should be checked both on the physical display and in
  Home Assistant when relevant.

## Useful Files

- `MerlinCCU.cpp`
  Main loop and module wiring.
- `display.*`
  Raster generation, PIO scanout, and DMA handoff.
- `framebuffer.*`
  UI drawing primitives and text rendering.
- `wifi_manager.*`
  Pico W connection management and internet probe state.
- `home_assistant_manager.*`
  REST-based Home Assistant status integration.
- `mqtt_manager.*`
  MQTT discovery and retained sensor publishing.
