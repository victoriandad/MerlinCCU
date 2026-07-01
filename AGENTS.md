# MerlinCCU Agent Notes

Persistent guidance for Codex, ChatGPT, and VS Code Codex sessions working on
this Raspberry Pi Pico 2 W embedded C++ firmware.

## Agent Workflow

- For non-trivial changes, review the relevant code and nearby docs before
  editing.
- Present a concise implementation plan before major implementation and wait
  for approval unless the user explicitly asks you to proceed directly.
- Keep changes scoped to the requested behaviour; avoid broad refactors,
  formatting churn, or unrelated documentation edits.
- When adding a user-visible feature, evaluate whether it introduces a failure
  mode that should surface as a CCU alert.
- Where suitable signals already exist in `ConsoleState`, implement alert logic
  in `console_controller::sync_system_alerts()` with a clear summary and
  operator-facing detail text.
- If the signal does not yet exist, add an explicit placeholder alert hook with
  a comment describing the missing telemetry so the alert can be completed
  later.
- Before changing UI behaviour, review `docs/ui_guidelines.md`,
  `docs/ui_patterns.md`, and `docs/display-test-checklist.md`.
- GitHub Issues are the preferred place to track bugs, enhancements, technical
  debt, feature requests, and hardware test findings.

## Documentation Map

- Architecture and ownership: `docs/architecture.md`
- Near-term architecture preparation: `docs/architecture-prep.md`
- UI principles: `docs/ui_guidelines.md`
- Reusable UI patterns: `docs/ui_patterns.md`
- Alert policy: `docs/alerts.md`
- Hardware findings: `docs/schematic-pinout.md`
- Display validation: `docs/display-test-checklist.md`
- Development workflow and coding standards: `docs/development.md`
