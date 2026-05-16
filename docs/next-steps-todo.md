# MerlinCCU Next Steps Todo

This checklist is the working plan for the next few development sessions. Keep
items small enough to flash, test, and commit in slices.

## Session 1: Stabilise Current Branch

- [ ] Flash the latest firmware and retest the physical `ALERT` key.
- [ ] Confirm `STATUS -> KEYPAD DEBUG` shows `KEY PRESSED ALERT` and active
      panel pins `5 20` when the physical alert key is pressed.
- [ ] Retest the web preview alert button and alert-page softkeys.
- [ ] Capture or refresh framebuffer baselines for changed Status and Alert
      pages.
- [ ] Review the current uncommitted changes and split them into sensible
      commits:
      - [ ] environment sensor/I2C scaffold
      - [ ] status-page diagnostics
      - [ ] alert-key wake fix

Done when: the current branch builds, flashes, key diagnostics match the bench
matrix, and the changed UI captures are either accepted or explicitly deferred.

## Session 2: Console Controller Split

- [ ] Extract alert model/rules from `src/core/console_controller.cpp`.
- [ ] Keep public behaviour unchanged while moving alert helpers.
- [ ] Add or prepare host-test seams for alert ordering, acknowledgement, and
      suppression.
- [ ] Extract softkey map building only after alert extraction is stable.
- [ ] Check flash/RAM size after each extraction.

Done when: alert workflow still behaves the same, `console_controller.cpp` is
smaller, and alert logic has a clearer ownership boundary.

## Session 3: Environment Sensor Bring-Up Prep

- [ ] Confirm final non-clashing I2C pins before enabling
      `config/environment_sensor_config.h`.
- [ ] Add BME280/BME680 calibration loading.
- [ ] Add forced-mode BME sample reads.
- [ ] Add compensated temperature, humidity, and pressure values.
- [ ] Mirror only compact sensor readings into `ConsoleState`.
- [ ] Add stale-reading and sensor-fault alert hooks.

Done when: the Waveshare board can identify the BME variant and produce bounded,
display-ready environmental readings without increasing `ConsoleState` with raw
driver internals.

## Session 4: Memory And Performance Budget

- [ ] Create a static RAM budget table covering display buffers, network
      buffers, `ConsoleState`, sensor state, and screensavers.
- [ ] Record `arm-none-eabi-size build\MerlinCCU.elf` after each feature slice.
- [ ] Identify one candidate large buffer to reduce, reuse, or sequence.
- [ ] Add a rule for when new static buffers need a size note.
- [ ] Review whether current loop timings justify any core 1 work.

Done when: RAM growth is visible before it becomes a problem, and multicore work
has a measured reason rather than being speculative.

## Session 5: Test Coverage Foundation

- [ ] Add a minimal host-test target for pure logic that does not need Pico
      hardware.
- [ ] Cover keypad matrix decode with known bench closures.
- [ ] Cover alert list ordering and acknowledgement behaviour.
- [ ] Cover environment sensor health classification.
- [ ] Cover configuration clamping/parsing where it is already pure enough to
      isolate.

Done when: future refactors can be checked without relying only on flashing the
CCU.

## Later Candidates

- [ ] Split sensor drivers by chip: BME, SGP40, TSL2591, LTR390, ICM20948.
- [ ] Feed BME temperature/humidity compensation into SGP40 once both drivers
      exist.
- [ ] Move sensor polling or slow compensation to core 1 only after the
      multicore checklist in `docs/architecture-prep.md` is satisfied.
- [ ] Reduce display or screensaver RAM if measured headroom becomes tight.
- [ ] Decide whether shared UI-facing snapshot types should move out of manager
      headers into a neutral model header.
