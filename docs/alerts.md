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

## Alert History

Past alerts are kept so the operator can see what happened overnight or
before a restart (#203). `alert_history` holds them and
`alert_history_store` keeps them in flash.

- **When an entry is added:** when an alert's condition clears (`Cleared`)
  or the operator accepts it (`Accepted`). An accepted alert whose condition
  clears later adds nothing more. `IGNORE` leaves the alert active; its
  entry, once it ends, records that it was ignored and when.
- **Repeats:** the same alert ending again within an hour of its entry being
  raised raises that entry's count and moves it to the top, rather than
  adding another. Without a synced clock entries never fold.
- **What is kept:** summary, severity, raised, ended and ignored times (local
  minutes since the epoch, 0 before the clock syncs), how it ended, the
  count, and the full detail text.
- **Size:** the newest 16 entries (two History pages); the oldest drops off
  (NAV-6b). The whole history is about 6 KB, kept in PSRAM on the Pimoroni
  and in SRAM on a Pico 2 W.
- **Persistence:** two copies of two flash sectors each, below the Pinter
  store, written alternately with a rising sequence and a CRC, so a power cut
  mid-save leaves the previous copy. A save follows a new entry or
  `CLEAR ALL` after at least a minute since the last save, and a fold after
  at least 10 minutes, to bound flash wear from a flapping alert. History
  that has not been saved yet is lost if the unit restarts in that window.
- **Clear:** History's `CLEAR ALL`, after a confirm step. Active alerts are
  not affected.
- **Remote:** `/alerts` on the web page shows active and past alerts with
  their full text; `/api/alerts` lists them one tab-separated line each.

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

An active alert points at its detail rather than holding a copy. Fixed
details are pointed at directly; details built at run time (Dynamic detail
below) share six buffers (`kDetailSlotCount` in `alert_controller.cpp`),
freed when the alert clears or is accepted. With all six taken, a seventh
such alert shows that there are too many alerts in place of its detail. A
new alert code must say which kind it is in `detail_is_fixed()`.

Debounced conditions count one-second samples, taken by the main loop through
`console_controller::sample_alerts()`, not `sync()` calls: sync runs on every
state change and twice per key press. A lost connection or unsynced clock must
last `kConnectionGraceSamples` (30 s), and a keypad fault `kKeypadFaultSamples`
(5 s), before it is raised; it clears on the first sync that sees it gone.
`DisplayPipelineLag` counts its own timing samples (`kAlertRetryThreshold`, 5).

| Code | Severity | Source Signal | Summary | Detail Policy | Test Notes |
| --- | --- | --- | --- | --- | --- |
| `WifiDisconnected` | Warning | `wifi_status.state != Connected` | `NETWORK` | Static text | Fires immediately, no debounce |
| `WifiAuthFailed` | Alert | `wifi_status.state == AuthFailed` | `WIFI AUTH` | Static text | Can be active alongside `WifiDisconnected` |
| `TimeNotSynced` | Warning | `!time_status.synced` while Wi-Fi connected, debounced | `TIME` | Static text | 30 s debounce |
| `HomeAssistantOffline` | Message | HA enabled, not Connected/Unauthorized, debounced | `HOME ASSISTANT` | Static text | 30 s debounce |
| `HomeAssistantUnauthorized` | Alert | `home_assistant_status.state == Unauthorized` | `AUTH FAILED` | Static text | Fires immediately, no debounce |
| `HomeAssistantEntityMissing` | Warning | tracked entity state empty/`unknown`/`unavailable` while HA connected | `HA ENTITY` | Static text | Requires a configured tracked entity id |
| `WeatherUnavailable` | Message | no weather while its source is reachable, debounced: for Home Assistant, the MQTT link is up but no weather has arrived (#153); for Open-Meteo, the internet is reachable but fetches fail | `WEATHER` | Static text | 30 s debounce. A lost MQTT link is not a weather alert. |
| `WeatherProviderWarning` | Provider-supplied (falls back to Warning) | `weather_alert_status.provider_warning_active` | Provider-supplied (falls back to `WX WARNING`) | Dynamic, provider-supplied summary/detail | Raised by severe conditions in the forecast (thunder, hail); official warnings are `OfficialWeatherWarning` |
| `OfficialWeatherWarning` | Yellow: Message, Amber: Warning, Red: Alert | `weather_warnings.level`: a Met Office weather warning or Environment Agency flood warning in force, from HA over MQTT (#202) | The highest warning, e.g. `AMBER WIND`, `YELLOW FOG`, `RED FLOOD` | Every warning in force, one per line, highest first, with when it ends or starts | Any weather source; needs MQTT and HA's `warnings:` block. Flood alert/warning/severe warning map to Yellow/Amber/Red. If HA has not refreshed them for 2 hours (`kWarningsStaleAfterMs`) the detail starts `Not updated for over 2 hours.` rather than dropping them |
| `WeatherTemperatureWarning` | Warning | current/forecast temperature <= 0C or >= 28C | `WX TEMP` | Dynamic detail (min/max C) | Thresholds shared with the Weather page highlighting (`weather_extremes.h`, below) |
| `WeatherWindWarning` | Warning | current/forecast mean wind >= 39mph or gusts >= 50mph | `WX WIND` | Dynamic detail (max mph, and gusts when the source gives them) | Thresholds shared with the Weather page highlighting (`weather_extremes.h`, below) |
| `MqttOffline` | Message | MQTT enabled, not Connected, debounced | `MQTT` | Static text | 30 s debounce |
| `KeypadLineFault` | Warning | `MULTI` pressed-key or `active_count > 3`, debounced | `KEYPAD` | Static text | 5 s debounce |
| `EnvironmentSensorFault` | Warning | sensor enabled and `Fault`/`BoardMissing`/`Partial` health, or a BME/SGP40 read error | `ENV SENSOR` | Static text | Fires immediately, no debounce |
| `LocalPressureStormWarning` | Warning | absolute low pressure or a rapid fall across recent 5-minute averages | `STORM WARN` | Dynamic detail (current pressure / fall amount) | See `kStormLowPressurePa`/`kStormRapidPressureFallPa` |
| `DisplayPipelineLag` | Warning | `display_timing_status.frame_rate_hz` below 75% of `nominal_frame_rate_hz`, debounced per one-second timing sample | `DISPLAY LAG` | Dynamic detail (measured and expected Hz) | 5-sample debounce counted on `sample_sequence`, not `sync()` calls; see `kDisplayFrameRateFloorPercent`. Bench: 297 Hz measured and nominal on a Pico 2 W at 150 MHz, clkdiv 2.51 |
| `StackLow` | Warning | `stack_status.free_bytes` (worst case since boot) below `kStackLowFreeBytes` (1 KB), or `stack_status.overflow_reset` (the previous reset was the stack-limit fault) | `STACK LOW` | Dynamic detail (free bytes, or that the CCU restarted after an overflow) | No debounce: the figure only falls across a boot. Overflow flag comes from `stack_guard` via a watchdog scratch register. Bench: about 2.8 KB of the 8 KB stack used (#164) |
| `HeapLow` | Warning | the heap's size (RAM left after the static image) minus `heap_status.peak_arena_bytes` (the largest arena since boot) below `kHeapLowFreeBytes` (8 KB) (#206) | `HEAP LOW` | Dynamic detail (bytes left at the peak, of the heap's size) | A fixed floor, since a new connection needs a fixed amount. The arena holds a peak between the one-second samples, so a brief one counts; it holds for the boot, and accepting it lasts the boot. Bench: about 9.6 KB of arena with HA, MQTT and the web server connected (Plus 2 W); a Pico 2 W's heap is about 27 KB |
| `ShareDataUnavailable` | Message | share data configured, invalid, and a real error/HTTP failure | `SHARES` | Static text | -- |
| `PinterScheduleConflict` | Message | placeholder, hardcoded `false` | `PINTER` | Empty detail | Never fires yet -- see Missing Telemetry |
| `HomeAssistantMessage` | From HA (default Message) | `ha_message.active`: HA sent a message over MQTT (#150) | HA's title, or `HA MESSAGE` | HA's text | Accepting it is reported to HA as Message state `acknowledged`; a new message shows again; empty text clears it |
| `FirmwareUpdateRolledBack` | Warning | `firmware_status.rolled_back`: this boot followed an uploaded image that did not confirm itself, and the boot ROM went back to the previous one | `UPDATE FAILED` | Static text | Holds for the whole boot. The record lives in a POWMAN scratch register, so a power cut during the trial goes unreported (docs/ota.md) |
| `UnexpectedRestart` | Warning | `boot_status.reset_reason` is `Watchdog`, `CpuFault` or `Brownout` (#155) | `RESTARTED` | Dynamic detail (the cause, and what to check) | Holds for the whole boot, so a restart overnight is seen. A stack overflow raises `StackLow` instead; power-on, reset pin, update (over Wi-Fi or a USB/picotool load, #204), rollback and asked-for restarts raise nothing |
| `ControlFailed` | Warning | `home_controls.last_failure`: the last answered Controls key press (#152) was refused by HA or got no answer within 5 s | `CONTROL FAILED` | Dynamic detail (the entity, and whether HA refused or was silent) | Holds until a Controls press works, so it outlasts the page's 10-second notice |
| `UnitOffline` | Warning | `unit_fleet.offline_alert_mask`: another unit on the broker has been offline for `kOfflineAlertMs` (30 minutes) and nobody has accepted it (#210) | `UNIT OFFLINE` | Dynamic detail (the units' names) | Accepting it on any unit publishes `<base>/<mac>/offline_ack`, which clears it on every unit until that unit is back; its next outage raises it again. Timed from when this unit learnt of the outage, so a unit that restarts during one waits 30 minutes afresh. Not raised while this unit's MQTT link is down (that is `MQTT`) |
| `MixedFirmware` | Message | `unit_fleet.mixed_firmware`: an online unit has run different firmware from this one for `kMixedFirmwareMs` (10 minutes) (#210) | `MIXED FIRMWARE` | Dynamic detail (each unit's version) | Long enough for an update of every unit to finish. Offline units, and units that publish no summary, do not count |

## Exceptional Weather Thresholds

The Weather page inverts a forecast cell whose value is exceptional for the
UK (#202), and `WX TEMP` and `WX WIND` use the same figures, so a highlighted
cell and its alert always agree. They live in one table,
`include/core/weather_extremes.h`.

| Hazard | Cell | Hour | Day | Basis |
| --- | --- | --- | --- | --- |
| Cold | Temp | <= 0 C | low <= 0 C | Frost and ice |
| Heat | Temp | >= 28 C | high >= 28 C | Top of the Met Office's county heatwave thresholds (25-28 C) |
| Wind | Wind | mean >= 39 mph or gust >= 50 mph | the same, on the day's figures | Beaufort 8 (gale) starts at 39 mph; 50 mph gusts are where yellow wind warnings usually start inland |
| Heavy rain | Conditions | >= 4 mm | >= 20 mm | Met Office "heavy rain" is over 4 mm/h; 20 mm in a day is typical of a yellow rain warning |
| Heavy snow | Conditions | >= 1 cm | >= 5 cm | 5 cm lying is where yellow snow warnings usually start |
| Dense fog | Conditions | visibility < 200 m | never | Met Office forecasts call fog under 200 m "dense" |

- Where a source gives no amount, the condition stands in: HA's `pouring` and
  Open-Meteo codes 65 and 82 are heavy rain; Open-Meteo codes 75 and 86 are
  heavy snow.
- HA forecasts have no snowfall field, so a `snowy` period's precipitation is
  read as snow at 1 cm per mm.
- Only Open-Meteo gives visibility, hourly only, so dense fog is highlighted
  on Open-Meteo hourly rows alone; Met Office fog warnings cover it otherwise.
- Gusts come from Open-Meteo and from HA providers that send them (the Met
  Office integration does; Met.no does not).
- On Next 24 Hours, a sampled hour with nothing exceptional gives way to the
  first exceptional hour since the previous row, so sampling never hides one.

## Missing Telemetry

| Feature | Missing Signal | Placeholder Location | Issue |
| --- | --- | --- | --- |
| `PinterScheduleConflict` | Future fridge/dock reservation windows and Friday target forecasts | `alert_controller.cpp`, `sync()` -- `set_alert_condition(..., AlertCode::PinterScheduleConflict, false, ...)` | None yet. The Pinter workflow records typed queue entries and planned durations, but not future reservation windows to detect conflicts against. |

## Validation

- Exercise the alert list and detail pages through `/preview`.
- Verify physical alert-lamp behaviour when hardware wiring is available.
- Capture hardware test findings in GitHub Issues.
