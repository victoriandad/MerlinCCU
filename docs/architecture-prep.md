# Architecture Preparation Notes

These notes capture the near-term architecture rules before the environment
sensor work and any multicore split begin.

## Ownership Contracts

- Core UI state is owned by `console_controller`.
- Managers expose compact status snapshots rather than letting other modules
  mutate their private state.
- `ConsoleState` should stay display-facing. Do not add raw histories, large
  protocol buffers, calibration tables, or sensor-driver internals to it.
- Display ownership remains single-threaded: draw a complete back framebuffer,
  swap framebuffers, then call `display::present()`.
- Network and sensor managers should return `true` from `update()` only when a
  visible status snapshot has changed.

## Environment Sensor Direction

- The Waveshare Pico Environment Sensor board is treated as an optional
  subsystem behind `environment_sensor_manager`.
- I2C configuration is local-machine/harness-specific and lives in
  `config/environment_sensor_config.h`, copied from the example file.
- Sensor drivers should be concrete, allocation-free classes with static
  lifetime where practical.
- The first hardware milestone is bus discovery and a diagnostic snapshot. The
  BME280/BME680 path starts with chip-ID probing before calibration loading and
  compensation maths are added. Full readings should follow as separate drivers
  for BME280/BME680, SGP40, TSL2591, LTR390, and ICM20948.
- SGP40 compensation should consume temperature and humidity from the BME sensor
  when that driver exists.
- Future CCU alerts should be based on compact signals: board missing, stale
  readings, failed compensation, or operator-relevant thresholds.

## Multicore Admission Checklist

Do not move work to core 1 until these are true:

- The work has a single owner and communicates with core 0 through a queue or
  immutable snapshot.
- The work does not write `ConsoleState`, framebuffer memory, display state, or
  Pico SDK flash state directly.
- Stack usage has been estimated for the background task.
- Flash writes are protected so the second core is not executing from flash
  during erase/program operations.
- lwIP/CYW43 calls remain on their current owner unless explicit locking and
  callback-context rules have been reviewed.

Good first candidates for core 1 are sensor polling, compensation maths,
payload parsing, and slow data-reduction jobs. Display scanout, UI routing, and
button handling should remain on core 0.

## Memory Rules

- Prefer bounded arrays and compact snapshots.
- Reuse or sequence large network buffers before adding new static buffers.
- Keep rolling histories in the manager that owns them, not in `ConsoleState`.
- Add a size-map check when a new subsystem adds static storage.
