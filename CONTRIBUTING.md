# Contributing

## Local development

Prerequisite: Node.js 24 or newer. The initial Gateway prototype has no npm dependencies.

```powershell
npm run check
npm run dev
```

Open `http://127.0.0.1:4173` for the console. In another terminal run `npm run simulate` to register a mock probe. Stop the simulator with Ctrl+C; it unregisters its device.

The Probe C5 project additionally requires an activated ESP-IDF installation that supports ESP32-C5. Its build instructions are in `firmware/probe-c5/README.md`.

## Changes

- Work on a focused branch; use separate Git worktrees for concurrent tasks that touch overlapping files.
- Keep public cross-project changes in `contracts/`, and update the simulator and tests in the same change.
- Do not check in local SDK configuration, secrets, build artifacts, or unverified hardware claims.
- Run `npm run check` and `git diff --check` before proposing a change. Report any unavailable toolchain or hardware check.
- Do not publish, push, create a release, or modify repository settings without authorization.
