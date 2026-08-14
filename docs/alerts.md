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
- Active alerts carry severity, summary, detail text, occurrence time, and a
  sequence for acknowledgement behaviour.
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
| `WeatherUnavailable` | Message | weather refresh failed, debounced | `WEATHER` | Static text | 5-sample debounce |
| `WeatherProviderWarning` | Provider-supplied (falls back to Warning) | `weather_alert_status.provider_warning_active` | Provider-supplied (falls back to `WX WARNING`) | Dynamic, provider-supplied summary/detail | Official provider-warning APIs not wired yet; only severe-condition telemetry (e.g. thunder) currently raises this |
| `WeatherTemperatureWarning` | Warning | current/forecast temperature <= 0C or >= 30C | `WX TEMP` | Dynamic detail (min/max C) | See `kFreezingTemperatureAlertCelsius`/`kHighTemperatureAlertCelsius` |
| `WeatherWindWarning` | Warning | current/forecast wind >= 40mph | `WX WIND` | Dynamic detail (max mph) | See `kHighWindAlertMph` |
| `MqttOffline` | Message | MQTT enabled, not Connected, debounced | `MQTT` | Static text | 5-sample debounce |
| `KeypadLineFault` | Warning | `MULTI` pressed-key or `active_count > 3`, debounced | `KEYPAD` | Static text | 5-sample debounce |
| `EnvironmentSensorFault` | Warning | sensor enabled and `Fault`/`BoardMissing`/`Partial` health, or a BME/SGP40 read error | `ENV SENSOR` | Static text | Fires immediately, no debounce |
| `LocalPressureStormWarning` | Warning | absolute low pressure or a rapid fall across recent 5-minute averages | `STORM WARN` | Dynamic detail (current pressure / fall amount) | See `kStormLowPressurePa`/`kStormRapidPressureFallPa` |
| `DisplayPipelineLag` | Message | placeholder, hardcoded `false` | `DISPLAY LAG` | Static text | Never fires yet -- see Missing Telemetry |
| `ShareDataUnavailable` | Message | share data configured, invalid, and a real error/HTTP failure | `SHARES` | Static text | -- |
| `PinterScheduleConflict` | Message | placeholder, hardcoded `false` | `PINTER` | Empty detail | Never fires yet -- see Missing Telemetry |

## Missing Telemetry

| Feature | Missing Signal | Placeholder Location | Issue |
| --- | --- | --- | --- |
| `DisplayPipelineLag` | Frame-timing/render-lag counter | `alert_controller.cpp`, `sync()` -- `const bool display_pipeline_lag = false;` | None yet. Note: `DisplayTimingStatus.present_skipped_count` was added to `ConsoleState` under issue #71, so the underlying telemetry this placeholder was waiting for now exists; wiring it up is a candidate follow-up, not yet filed. |
| `PinterScheduleConflict` | Future fridge/dock reservation windows and Friday target forecasts | `alert_controller.cpp`, `sync()` -- `set_alert_condition(..., AlertCode::PinterScheduleConflict, false, ...)` | None yet. The Pinter workflow records typed queue entries and planned durations, but not future reservation windows to detect conflicts against. |

## Validation

- Exercise the alert list and detail pages through `/preview`.
- Verify physical alert-lamp behaviour when hardware wiring is available.
- Capture hardware test findings in GitHub Issues.
