# MerlinCCU Agent Notes

Persistent guidance for Codex, ChatGPT, and VS Code Codex sessions working on
this Raspberry Pi Pico 2 W embedded C++ firmware.

## Collaboration Workflow

- For non-trivial changes, review the relevant code and nearby docs before
  editing.
- Present a concise implementation plan before major implementation and wait
  for approval unless the user explicitly asks you to proceed directly.
- Keep changes scoped to the requested behaviour; avoid broad refactors,
  formatting churn, or unrelated documentation edits.
- GitHub Issues are the preferred place to track bugs, enhancements, technical
  debt, feature requests, and hardware test findings.

## Build Workflow

- CLI builds are allowed in this repo.
- Before terminal builds, configure Pico SDK tool paths first: `PICO_SDK_PATH`,
  `PICO_TOOLCHAIN_PATH`, and `PATH` entries for the ARM toolchain, Ninja, and
  CMake. Command discovery should match the VS Code Pico extension environment.
- Keep CLI checks bounded to one configure/build pass so the first actionable
  error returns quickly.

## Coding Standards

- Use C++17 or newer within the repo's existing Pico SDK constraints.
- Use British English in documentation, comments, and user-facing strings, but
  do not rename identifiers, protocol/API fields, CSS keywords, generated code,
  or third-party interfaces only for spelling.
- Prefer standard library facilities and typed constructs: `constexpr`,
  `enum class`, `std::array`, `std::string_view`, `std::optional`, `std::min`,
  `std::max`, and `std::clamp`.
- If a custom helper is needed for non-standard behaviour, document why the
  standard facility is unsuitable.
- Prefer automatic storage, `std::array`, and static lifetime where ownership
  is fixed. Use `std::unique_ptr` only when dynamic ownership is required, and
  avoid `std::shared_ptr` unless shared lifetime is genuinely needed.
- Do not introduce `#define` constants when `constexpr`, `const`, or
  `enum class` would work. Reserve all-caps names for user-editable config
  symbols or third-party/generated interfaces.
- Use uppercase literal suffixes such as `U`, `UL`, `ULL`, and `F` when a
  suffix is needed.
- Name non-obvious limits, protocol values, geometry, timings, thresholds, and
  buffer sizes. Avoid unexplained magic numbers.
- Use `PascalCase` for types and enum values, `snake_case` for functions and
  ordinary variables, `g_snake_case` for namespace-scope mutable state, and
  `kCamelCase` for repo-internal constants and `constexpr` data.
- Always use braces for `if`, `else`, `for`, and `while`, even for single-line
  bodies.
- Prefer deleting unused code and reducing duplication over keeping dead paths
  "just in case".
- Respect `.clang-format`, `.clang-tidy`, and `.editorconfig`.

## Comments and Documentation

- Add Doxygen-style headers to public functions in headers and to internal
  functions with non-trivial behaviour, constraints, assumptions, side effects,
  hardware context, timing sensitivity, state machines, or protocol handling.
- Prefer comments that explain why an approach exists, what constraints matter,
  and which hardware or protocol behaviour drives it. Avoid line-by-line
  narration of obvious code.
- Put detailed project knowledge in focused docs instead of growing this file:
  architecture in `docs/architecture.md`, UI principles in
  `docs/ui_guidelines.md`, reusable UI components in `docs/ui_patterns.md`,
  alert policy in `docs/alerts.md`, hardware findings in
  `docs/schematic-pinout.md`, and display validation in
  `docs/display-test-checklist.md`.

## UI Guidance

- Treat the firmware UI as an operator panel for a constrained monochrome
  framebuffer, not a general web UI.
- Preserve shared page chrome, softkey-first navigation, compact status rows,
  mixed-case values, uppercase headings/labels, and unclipped text.
- Before changing UI behaviour, review `docs/ui_guidelines.md`,
  `docs/ui_patterns.md`, and `docs/display-test-checklist.md`.

## Alert Policy

- When adding a user-visible feature, evaluate whether it introduces a failure
  mode that should surface as a CCU alert.
- Where suitable signals already exist in `ConsoleState`, implement alert logic
  in `console_controller::sync_system_alerts()` with a clear summary and
  operator-facing detail text.
- If the signal does not yet exist, add an explicit placeholder alert hook with
  a comment describing the missing telemetry so the alert can be completed
  later.
