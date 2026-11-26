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

## Code quality

- Gateway, console, simulator, and new host integration code use TypeScript with strict checking; add new host code as `.ts`. Existing Python provisioning and HIL scripts remain supported, keep request parsing and resource limits at the boundary, and let errors reach the caller instead of returning a false success.
- Keep queues, buffers, retries, timeouts, and leases bounded. New stateful or security-sensitive behavior needs a focused regression test covering its failure path.
- C5 component boundaries, C11/C++17 rules, public header placement, and formatting are defined in [Probe C5 编程规范](firmware/probe-c5/docs/coding-standards.md). Run the documented host tests, ESP-IDF build, and clang-format check for firmware changes.
