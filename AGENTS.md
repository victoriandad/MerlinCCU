# MerlinCCU Agent Notes

## Build Workflow

- Do not run `cmake`, `cmake --build`, or other build commands from the CLI in this repo.
- This setup is built and flashed from the VS Code Pico extension, not from the terminal.
- If build verification is needed, ask the user to run it from the extension instead of attempting a CLI build locally.

## Coding Standards

- Follow C++17 as the minimum language standard for code changes.
- Prefer `constexpr`, `enum class`, `std::array`, `std::string_view`, and other typed standard-library constructs over legacy C-style patterns where appropriate.
- Prefer automatic storage, `std::array`, and static lifetime where ownership is fixed. If dynamic ownership is genuinely needed, prefer `std::unique_ptr` over raw owning pointers, and avoid `std::shared_ptr` unless shared lifetime is truly required.
- Use `PascalCase` for types and enum values, `snake_case` for functions and ordinary variables, `g_snake_case` for namespace-scope mutable state, and `kCamelCase` for named constants and `constexpr` data.
- Use `kCamelCase` for repo-internal named constants and `constexpr` data. Reserve all-caps names for user-editable config symbols or third-party/generated interfaces when compatibility requires them.
- Use uppercase literal suffixes such as `U`, `UL`, `ULL`, and `F` for numeric literals when a suffix is needed.
- Do not introduce `#define` constants when `constexpr`, `const`, or `enum class` would work.
- Avoid unexplained magic numbers. Name non-obvious limits, protocol values, geometry values, timings, and buffer-related constants unless the literal is truly self-evident.
- Add meaningful comments throughout the code to support readability and long-term maintainability, especially for newcomers reading the firmware for the first time.
- Do not rely on "self-documenting code" as a substitute for comments in non-trivial logic, hardware-facing code, state machines, timing-sensitive paths, or protocol handling.
- Add Doxygen-style comment headers to public functions in headers and to internal functions that contain non-trivial behavior, constraints, assumptions, or side effects.
- Prefer comments that explain why a block exists, why an approach was chosen, what constraints or assumptions matter, and what hardware or protocol context drives the implementation.
- Avoid comments that merely narrate what the code is doing line by line when that is already obvious from the code itself.
- Always use braces for `if`, `else`, `for`, and `while` bodies, even when the body is a single statement.
- Respect the repo `.clang-format`, `.clang-tidy`, and `.editorconfig` files when making style or cleanup changes.
- Avoid broad formatting-only churn unless the user explicitly asks for a formatting pass.
