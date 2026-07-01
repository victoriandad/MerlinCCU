# MerlinCCU Contributing Notes

This repo is still hardware bring-up firmware, so small disciplined changes are
better than broad refactors.

For build setup, coding standards, comment guidance, and validation rules, see
`docs/development.md`.

## Setup Checklist

1. Copy `config/wifi_credentials.example.h` to
   `config/wifi_credentials.h` and fill in your Wi-Fi settings.
2. If you want Home Assistant REST support, copy
   `config/home_assistant_credentials.example.h` to
   `config/home_assistant_credentials.h`.
3. If you want Home Assistant MQTT discovery, copy
   `config/mqtt_credentials.example.h` to `config/mqtt_credentials.h`.
4. Keep those local credential files out of version control.

## Working Rules

- Branch from `main` for each feature or cleanup.
- Keep network secrets, tokens, broker passwords and local IP choices in the
  ignored local headers, never in tracked files.
- Prefer updating public headers and example files when behaviour changes.
- Track open work in GitHub Issues rather than in `README.md`.
- When a change needs follow-up work, add or update the relevant issue instead
  of growing the README backlog.
- Keep the repository's development rules in `docs/development.md` and the
  domain rules in the focused docs under `docs/`.

## Useful Files

- `src/core/MerlinCCU.cpp`
  Main loop and module wiring.
- `src/display/display.*`
  Raster generation, PIO scanout, and DMA handoff.
- `src/display/framebuffer.*`
  UI drawing primitives and text rendering.
- `src/network/wifi_manager.*`
  Pico W connection management and internet probe state.
- `src/network/home_assistant_manager.*`
  REST-based Home Assistant status integration.
- `src/network/mqtt_manager.*`
  MQTT discovery and retained sensor publishing.
