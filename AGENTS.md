# MerlinCCU Agent Notes

## Build Workflow

- Do not run `cmake`, `cmake --build`, or other build commands from the CLI in this repo.
- This setup is built and flashed from the VS Code Pico extension, not from the terminal.
- If build verification is needed, ask the user to run it from the extension instead of attempting a CLI build locally.
