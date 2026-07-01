# Alerts

## Why This Exists

Define how operator-visible CCU alerts should be added, reviewed, and tested so
failure modes are handled consistently across firmware features.

## What Belongs Here

- Alert severity policy and wording rules.
- Required telemetry for each alert.
- Implementation checklist for `console_controller::sync_system_alerts()`.
- Placeholder policy for missing signals.
- Test and hardware-validation notes.

## Current Alert Model

- Alert state is owned by `console_controller`.
- Alert conditions are synchronised in `sync_system_alerts()`.
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
  `console_controller::sync_system_alerts()`.
- Add placeholder hooks with comments when telemetry is not available yet.
- Confirm alert lamp behaviour, list entry, detail text, accept flow, ignore
  flow, and re-alert behaviour.

## Alert Catalogue

| Code | Severity | Source Signal | Summary | Detail Policy | Test Notes |
| --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |

## Missing Telemetry

| Feature | Missing Signal | Placeholder Location | Issue |
| --- | --- | --- | --- |
|  |  |  |  |

## Validation

- Exercise the alert list and detail pages through `/preview`.
- Verify physical alert-lamp behaviour when hardware wiring is available.
- Capture hardware test findings in GitHub Issues.
