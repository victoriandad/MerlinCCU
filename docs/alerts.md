# Alerts

## Why This Exists

Define how operator-visible CCU alerts should be added, reviewed, and tested so
failure modes are handled consistently across firmware features.

## What Belongs Here

- Alert severity policy and wording rules.
- Required telemetry for each alert.
- Implementation checklist for `alert_controller::sync()`.
- Placeholder policy for missing signals.
- Test and hardware-validation notes.

## Current Alert Model

- Alert state is owned by `alert_controller` (`src/core/alert_controller.cpp`,
  issue #44), operating on the `ConsoleState&` passed in from
  `console_controller`.
- Alert conditions are synchronised in `alert_controller::sync()`, via the
  internal `set_alert_condition()` helper.
- Active alerts carry severity, summary, detail text, occurrence time, an
  occurrence count, and a sequence for acknowledgement behaviour.
- Alert types are listed in `AlertCode` (`include/core/alert_codes.h`). The
  list holds at most one entry per type and is sized from `AlertCode::Count`,
  so adding a code grows the list and its page count with no other edit.
- Each time an alert's condition goes from clear to active, its occurrence
  count rises, even if the alert cleared in between. Once the count is above
  one, the alert key shows it after the time (`[12:34 x3]`) and the detail page
  ends with an "Occurred N times" line. The count stops at 99.
- ACCEPT hides the alert until its condition clears and resets its count;
  the next recurrence counts as the first. A restart also resets every count.
- The alert list is newest-first; detail pages support accept and ignore flows.

## Severity Guide

- `Message`: useful operator notice or degraded optional service.
- `Warning`: fault or environmental condition that may need attention soon.
- `Alert`: immediate operator attention or a blocking/security-sensitive fault.

## Wording Rules

- Summaries should fit softkey/list constraints and identify the subsystem.
- Detail text should explain what is wrong, why it matters, and what the
  operator can check next.
- Use British English in operator-facing text.
- Avoid raw error codes unless paired with readable context.

## Implementation Checklist

- Identify the user-visible failure mode.
- Confirm whether the required signal already exists in `ConsoleState`.
- Add retry/debounce thresholds for noisy or transient conditions.
- Implement or update `set_alert_condition()` calls in
  `alert_controller::sync()`.
- Add placeholder hooks with comments when telemetry is not available yet.
- Confirm alert lamp behaviour, list entry, detail text, accept flow, ignore
  flow, and re-alert behaviour.

## Alert Catalogue

All debounced conditions use `kAlertRetryThreshold` (5 consecutive failing
samples) before annunciating, to avoid flapping on transient failures.

| Code | Severity | Source Signal | Summary | Detail Policy | Test Notes |
| --- | --- | --- | --- | --- | --- |
| `WifiDisconnected` | Warning | `wifi_status.state != Connected` | `NETWORK` | Static text | Fires immediately, no debounce |
| `WifiAuthFailed` | Alert | `wifi_status.state == AuthFailed` | `WIFI AUTH` | Static text | Can be active alongside `WifiDisconnected` |
| `TimeNotSynced` | Warning | `!time_status.synced` while Wi-Fi connected, debounced | `TIME` | Static text | 5-sample debounce |
| `HomeAssistantOffline` | Message | HA enabled, not Connected/Unauthorized, debounced | `HOME ASSISTANT` | Static text | 5-sample debounce |
| `HomeAssistantUnauthorized` | Alert | `home_assistant_status.state == Unauthorized` | `AUTH FAILED` | Static text | Fires immediately, no debounce |
| `HomeAssistantEntityMissing` | Warning | tracked entity state empty/`unknown`/`unavailable` while HA connected | `HA ENTITY` | Static text | Requires a configured tracked entity id |
| `WeatherUnavailable` | Message | no weather while its source is reachable, debounced: for Home Assistant, the MQTT link is up but no weather has arrived (#153); for Open-Meteo, the internet is reachable but fetches fail | `WEATHER` | Static text | 5-sample debounce. A lost MQTT link is not a weather alert. |
| `WeatherProviderWarning` | Provider-supplied (falls back to Warning) | `weather_alert_status.provider_warning_active` | Provider-supplied (falls back to `WX WARNING`) | Dynamic, provider-supplied summary/detail | Official provider-warning APIs not wired yet; only severe-condition telemetry (e.g. thunder) currently raises this |
| `WeatherTemperatureWarning` | Warning | current/forecast temperature <= 0C or >= 30C | `WX TEMP` | Dynamic detail (min/max C) | See `kFreezingTemperatureAlertCelsius`/`kHighTemperatureAlertCelsius` |
| `WeatherWindWarning` | Warning | current/forecast wind >= 40mph | `WX WIND` | Dynamic detail (max mph) | See `kHighWindAlertMph` |
| `MqttOffline` | Message | MQTT enabled, not Connected, debounced | `MQTT` | Static text | 5-sample debounce |
| `KeypadLineFault` | Warning | `MULTI` pressed-key or `active_count > 3`, debounced | `KEYPAD` | Static text | 5-sample debounce |
| `EnvironmentSensorFault` | Warning | sensor enabled and `Fault`/`BoardMissing`/`Partial` health, or a BME/SGP40 read error | `ENV SENSOR` | Static text | Fires immediately, no debounce |
| `LocalPressureStormWarning` | Warning | absolute low pressure or a rapid fall across recent 5-minute averages | `STORM WARN` | Dynamic detail (current pressure / fall amount) | See `kStormLowPressurePa`/`kStormRapidPressureFallPa` |
| `DisplayPipelineLag` | Warning | `display_timing_status.frame_rate_hz` below 75% of `nominal_frame_rate_hz`, debounced per one-second timing sample | `DISPLAY LAG` | Dynamic detail (measured and expected Hz) | 5-sample debounce counted on `sample_sequence`, not `sync()` calls; see `kDisplayFrameRateFloorPercent`. Bench: 297 Hz measured and nominal on a Pico 2 W at 150 MHz, clkdiv 2.51 |
| `StackLow` | Warning | `stack_status.free_bytes` (worst case since boot) below `kStackLowFreeBytes` (1 KB), or `stack_status.overflow_reset` (the previous reset was the stack-limit fault) | `STACK LOW` | Dynamic detail (free bytes, or that the CCU restarted after an overflow) | No debounce: the figure only falls across a boot. Overflow flag comes from `stack_guard` via a watchdog scratch register. Bench: about 2.8 KB of the 8 KB stack used (#164) |
| `ShareDataUnavailable` | Message | share data configured, invalid, and a real error/HTTP failure | `SHARES` | Static text | -- |
| `PinterScheduleConflict` | Message | placeholder, hardcoded `false` | `PINTER` | Empty detail | Never fires yet -- see Missing Telemetry |
| `HomeAssistantMessage` | From HA (default Message) | `ha_message.active`: HA sent a message over MQTT (#150) | HA's title, or `HA MESSAGE` | HA's text | Accepting it is reported to HA as Message state `acknowledged`; a new message shows again; empty text clears it |
| `FirmwareUpdateRolledBack` | Warning | `firmware_status.rolled_back`: this boot followed an uploaded image that did not confirm itself, and the boot ROM went back to the previous one | `UPDATE FAILED` | Static text | Holds for the whole boot. The record lives in a POWMAN scratch register, so a power cut during the trial goes unreported (docs/ota.md) |
| `UnexpectedRestart` | Warning | `boot_status.reset_reason` is `Watchdog`, `CpuFault` or `Brownout` (#155) | `RESTARTED` | Dynamic detail (the cause, and what to check) | Holds for the whole boot, so a restart overnight is seen. A stack overflow raises `StackLow` instead; power-on, reset pin, update, rollback and asked-for restarts raise nothing |

## Missing Telemetry

| Feature | Missing Signal | Placeholder Location | Issue |
| --- | --- | --- | --- |
| `PinterScheduleConflict` | Future fridge/dock reservation windows and Friday target forecasts | `alert_controller.cpp`, `sync()` -- `set_alert_condition(..., AlertCode::PinterScheduleConflict, false, ...)` | None yet. The Pinter workflow records typed queue entries and planned durations, but not future reservation windows to detect conflicts against. |

## Validation

- Exercise the alert list and detail pages through `/preview`.
- Verify physical alert-lamp behaviour when hardware wiring is available.
- Capture hardware test findings in GitHub Issues.
