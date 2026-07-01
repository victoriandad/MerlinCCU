# MerlinCCU Agent Notes

Persistent guidance for Codex, ChatGPT, and VS Code Codex sessions on this
Raspberry Pi Pico 2 W firmware.

## Agent Workflow

- Review the relevant code and nearby docs before non-trivial changes.
- Present a concise implementation plan before major implementation.
- Wait for approval before major implementation unless explicitly instructed
  to proceed.
- Keep changes scoped to the requested behaviour.
- Prefer GitHub Issues for bugs, enhancements, technical debt, hardware test
  findings, documentation work, and future work.
- Documentation should describe the system.
- Issues should track future work.
- Add or update alerts when a user-visible feature introduces a failure mode.
- If the needed telemetry does not exist yet, add a placeholder alert hook
  with a comment describing the missing signal.
- Before changing UI behaviour, review `docs/ui_guidelines.md`,
  `docs/ui_patterns.md`, and `docs/display-test-checklist.md`.

## Build Workflow

- CLI builds are allowed.
- Before terminal builds, configure `PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`,
  and `PATH` entries for the ARM toolchain, Ninja, and CMake.
- Match the VS Code Pico extension environment when resolving tool paths.
- Keep CLI checks bounded to one configure/build pass.

## Engineering Rules

- Use C++17 or newer within the existing Pico SDK constraints.
- Prefer standard library facilities and typed constructs such as `constexpr`,
  `enum class`, `std::array`, `std::string_view`, `std::optional`,
  `std::min`, `std::max`, and `std::clamp`.
- Prefer automatic storage, `std::array`, and static lifetime where ownership
  is fixed.
- Use `std::unique_ptr` only when dynamic ownership is required; avoid
  `std::shared_ptr` unless shared lifetime is genuinely needed.
- Do not introduce `#define` constants when typed alternatives suffice.
- Use uppercase literal suffixes such as `U`, `UL`, `ULL`, and `F`.
- Name non-obvious limits, timings, thresholds, geometry, protocol values,
  and buffer sizes.
- Use `PascalCase` for types and enum values, `snake_case` for functions and
  ordinary variables, `g_snake_case` for namespace-scope mutable state, and
  `kCamelCase` for repo-internal constants and `constexpr` data.
- Always use braces for `if`, `else`, `for`, and `while`.
- Prefer deleting unused code and reducing duplication over keeping dead
  paths.
- Respect `.clang-format`, `.clang-tidy`, and `.editorconfig`.

## Documentation Rules

- Add Doxygen-style headers to public functions in headers and to internal
  functions with non-trivial behaviour, constraints, or hardware context.
- Prefer comments that explain why an approach exists, what constraints
  matter, and which hardware or protocol behaviour drives it.
- Keep detailed project knowledge in focused docs:
  - architecture: `docs/architecture.md`
  - UI rules: `docs/ui_guidelines.md`
  - UI patterns: `docs/ui_patterns.md`
  - alerts: `docs/alerts.md`
  - hardware findings: `docs/schematic-pinout.md`
  - display validation: `docs/display-test-checklist.md`

## UI Rules

- Treat the firmware UI as an operator panel for a constrained monochrome
  framebuffer.
- Preserve shared page chrome, softkey-first navigation, compact status rows,
  mixed-case values, uppercase headings and labels, and unclipped text.
- Use `docs/ui_guidelines.md` as the source of truth for UI behaviour.
