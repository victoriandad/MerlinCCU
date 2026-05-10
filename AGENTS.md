# MerlinCCU Agent Notes

## Build Workflow

- CLI builds are allowed in this repo.
- When running builds from a terminal, ensure Pico SDK tool paths are configured first
  (`PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`, and PATH entries for toolchain, Ninja,
  and CMake) so command discovery matches the VS Code Pico extension environment.
- Keep CLI build checks bounded (single configure/build pass, no repeated retries)
  so failures return quickly with the first actionable error.

## Coding Standards

- Follow C++17 as the minimum language standard for code changes.
- Use British English spelling in documentation, comments, and user-facing strings (for example: `behaviour`, `centre`, `colour`).
  - Do not change code identifiers, protocol/API field names, CSS keywords (for example `color`, `align-items:center`), or third-party interfaces purely for spelling consistency.
- Prefer standard library facilities over bespoke helpers when they meet the need (for example `std::clamp`, `std::min`, `std::max`, `std::optional`).
  - If a custom helper is needed for non-standard behaviour, document the behaviour difference and why the standard facility is unsuitable.
- "Less code is good code": remove unused code, avoid duplication, and prefer deleting dead paths over keeping them "just in case".
- Prefer `constexpr`, `enum class`, `std::array`, `std::string_view`, and other typed standard-library constructs over legacy C-style patterns where appropriate.
- Prefer automatic storage, `std::array`, and static lifetime where ownership is fixed. If dynamic ownership is genuinely needed, prefer `std::unique_ptr` over raw owning pointers, and avoid `std::shared_ptr` unless shared lifetime is truly required.
- Use `PascalCase` for types and enum values, `snake_case` for functions and ordinary variables, `g_snake_case` for namespace-scope mutable state, and `kCamelCase` for named constants and `constexpr` data.
- Use `kCamelCase` for repo-internal named constants and `constexpr` data. Reserve all-caps names for user-editable config symbols or third-party/generated interfaces when compatibility requires them.
- Use uppercase literal suffixes such as `U`, `UL`, `ULL`, and `F` for numeric literals when a suffix is needed.
- Do not introduce `#define` constants when `constexpr`, `const`, or `enum class` would work.
- Avoid unexplained magic numbers. Name non-obvious limits, protocol values, geometry values, timings, and buffer-related constants unless the literal is truly self-evident.
- Add meaningful comments throughout the code to support readability and long-term maintainability, especially for newcomers reading the firmware for the first time.
- Do not rely on "self-documenting code" as a substitute for comments in non-trivial logic, hardware-facing code, state machines, timing-sensitive paths, or protocol handling.
- Add Doxygen-style comment headers to public functions in headers and to internal functions that contain non-trivial behaviour, constraints, assumptions, or side effects.
- Prefer comments that explain why a block exists, why an approach was chosen, what constraints or assumptions matter, and what hardware or protocol context drives the implementation.
- Avoid comments that merely narrate what the code is doing line by line when that is already obvious from the code itself.
- Always use braces for `if`, `else`, `for`, and `while` bodies, even when the body is a single statement.
- Respect the repo `.clang-format`, `.clang-tidy`, and `.editorconfig` files when making style or cleanup changes.
- Avoid broad formatting-only churn unless the user explicitly asks for a formatting pass.

## Alert Policy

- When implementing a new user-visible feature, evaluate whether it introduces a failure mode that should surface as a CCU alert.
- Where suitable signals already exist in `ConsoleState`, implement the alert logic in `console_controller::sync_system_alerts()` with a clear summary and operator-facing detail text.
- If the signal does not yet exist, add an explicit placeholder alert hook with a comment describing the missing telemetry so the alert can be completed later.
