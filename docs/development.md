# Development Guide

## Why This Exists

Keep the repo-wide build, style, comment, and validation rules in one place so
contributors and Codex sessions do not have to rediscover them from chat
history.

## Build Workflow

- CLI builds are allowed in this repo.
- Before terminal builds, configure Pico SDK tool paths first:
  `PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`, and `PATH` entries for the ARM
  toolchain, Ninja, and CMake.
- Match the VS Code Pico extension environment when discovering command paths.
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

## Comments And Documentation

- Add Doxygen-style headers to public functions in headers and to internal
  functions with non-trivial behaviour, constraints, assumptions, side effects,
  hardware context, timing sensitivity, state machines, or protocol handling.
- Prefer comments that explain why an approach exists, what constraints matter,
  and which hardware or protocol behaviour drives it. Avoid line-by-line
  narration of obvious code.
- Put detailed project knowledge in focused docs instead of expanding this
  guide.

## Validation Expectations

- Firmware changes should be verified on hardware before merging.
- USB serial logs are often the fastest way to diagnose timing, Wi-Fi, Home
  Assistant, and MQTT issues.
- Status-page changes should be checked both on the physical display and in
  Home Assistant when relevant.
- If a change affects rendered output, page layout, status text, or preview
  behaviour, update display tests in the same change:
  - update `tools/framebuffer_suite.json` checks/masks as needed
  - capture or refresh affected baseline PBMs in `baselines/`
  - update `docs/display-test-checklist.md` when workflow expectations change

## No-Target Checks

When target hardware is unavailable, run the lightweight repository checks:

```powershell
python tools/host_checks.py
```

These checks validate data that can drift during safe documentation and
architecture work: tracked config headers, PBM baseline integrity, and
framebuffer regression suite metadata. They do not replace Pico builds,
`/preview` regression checks, or physical panel validation.

## Working Notes

- Keep comments focused on purpose and behaviour rather than narrating syntax
  line by line.
- Avoid large formatting-only churn unless the formatting pass is intentional
  and isolated.
- Prefer small behavioural commits. If a cleanup pass is separate from a
  functional change, keep them in separate commits where practical.
