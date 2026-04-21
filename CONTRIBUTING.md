# MerlinCCU Contributing Notes

This repo is still hardware bring-up firmware, so small disciplined changes are
better than broad refactors.

## Setup Checklist

1. Copy `wifi_credentials.example.h` to `wifi_credentials.h` and fill in your
   Wi-Fi settings.
2. If you want Home Assistant REST support, copy
   `home_assistant_credentials.example.h` to
   `home_assistant_credentials.h`.
3. If you want Home Assistant MQTT discovery, copy
   `mqtt_credentials.example.h` to `mqtt_credentials.h`.
4. Keep those local credential files out of version control.

## Working Rules

- Branch from `main` for each feature or cleanup.
- Keep network secrets, tokens, broker passwords and local IP choices in the
  ignored local headers, never in tracked files.
- Prefer updating public headers and example files when behavior changes.
- Write meaningful comments to help a new contributor understand intent, data
  flow, hardware assumptions, and why a block exists.
- Do not treat "the code comments itself" as sufficient justification for
  omitting comments in non-trivial code.
- Keep comments focused on purpose and behavior rather than narrating syntax
  line by line.
- Prefer comments that explain why a piece of code exists, why an implementation
  choice was made, and what constraints or assumptions shaped it.
- Avoid comments that only restate what the next line of code obviously does.
- Always use braces for `if`, `else`, `for`, and `while` bodies, even when the
  body is a single statement.
- Avoid large formatting-only churn.

## Coding Standards

- The minimum language standard for C++ code in this repo is C++17.
- Prefer `constexpr`, `enum class`, `std::array`, `std::string_view`, and
  other standard-library facilities over legacy C-style patterns when they
  improve clarity without adding unnecessary abstraction.
- Prefer automatic storage, `std::array`, and static lifetime where ownership
  is fixed. If dynamic ownership is genuinely needed, prefer
  `std::unique_ptr` over raw owning pointers, and avoid `std::shared_ptr`
  unless shared lifetime is truly required.
- Use `PascalCase` for types and enum values, `snake_case` for functions and
  ordinary variables, `g_snake_case` for namespace-scope mutable state, and
  `kCamelCase` for named constants and `constexpr` data.
- Use `kCamelCase` for repo-internal named constants and `constexpr` data.
  Reserve all-caps names for user-editable config symbols or
  third-party/generated interfaces when compatibility requires them.
- Use uppercase literal suffixes such as `U`, `UL`, `ULL`, and `F` when a
  numeric literal needs an explicit suffix.
- Avoid unexplained magic numbers. Prefer named constants for non-obvious
  limits, timings, protocol values, geometry values, and buffer-related
  constants unless the literal is genuinely self-evident.
- Do not use `#define` for constants or typed values. Use `constexpr`,
  `const`, `enum class`, or `inline constexpr` instead.
- Reserve preprocessor macros for header guards, compile-time feature checks,
  and cases where the preprocessor is genuinely required.
- Public functions declared in headers should have Doxygen-style comments.
- Non-trivial internal functions should also have Doxygen-style comments.
- Favor explanatory comments in state machines, hardware-facing code, timing
  paths, protocol handling, and other areas where a newcomer would otherwise
  need to reverse-engineer intent from implementation details.
- Keep naming, comments, and structure consistent with the existing file once
  you are editing inside it.
- Prefer small behavioral commits. If a cleanup pass is separate from a
  functional change, keep them in separate commits where practical.

## Formatting And Static Analysis

- Use the repo `.clang-format` as the formatting baseline.
- Use the repo `.clang-tidy` as the baseline for modernization and consistency
  checks.
- Use the repo `.editorconfig` for shared whitespace and line-ending defaults.
- Avoid mixing functional edits with broad automatic reformatting unless the
  formatting pass is intentional and isolated.

## Validation Expectations

- Firmware changes should be verified on hardware before merging.
- USB serial logs are often the fastest way to diagnose timing, Wi-Fi, Home
  Assistant, and MQTT issues.
- Status-page changes should be checked both on the physical display and in
  Home Assistant when relevant.

## Useful Files

- `MerlinCCU.cpp`
  Main loop and module wiring.
- `display.*`
  Raster generation, PIO scanout, and DMA handoff.
- `framebuffer.*`
  UI drawing primitives and text rendering.
- `wifi_manager.*`
  Pico W connection management and internet probe state.
- `home_assistant_manager.*`
  REST-based Home Assistant status integration.
- `mqtt_manager.*`
  MQTT discovery and retained sensor publishing.
