# TetherBench Agent Instructions

TetherBench is a local-first tool for managing embedded debug probes. The product is at `0.0.1-alpha`; `VERSION` is its only project-version source.

## Boundaries

- Keep the computer-side Gateway a modular monolith unless measured requirements justify a separate process.
- Treat `contracts/` as the authority for cross-project interfaces. The current HTTP API is a local simulation API, not a firmware protocol.
- Keep Probe C5, Bridge P4, Hosted C5, and STM32 target firmware independently buildable. Do not reference another firmware project's private source.
- Keep hardware transactions on the device that owns the pins and buses. Do not claim hardware support from simulator results.
- Mark decisions as verified, provisional, or open. Record technical decisions in `docs/adr/` and keep dated upstream evidence in `docs/upstream/`.

## Changes and verification

- Keep dependencies and toolchains to the minimum needed by the changed project. Do not commit SDKs, toolchain caches, credentials, or generated build output.
- Run `npm run check` for Gateway, simulator, contract, or console changes. Build firmware only in an activated, supported ESP-IDF environment and record the exact result.
- Add a focused regression test for new non-trivial behavior. Keep request sizes, queues, timeouts, and resource ownership bounded.
- Do not expose the unauthenticated prototype API beyond loopback. Real device authentication and authorization are not implemented.
- Do not change `VERSION`, create tags/releases, rewrite history, force-push, or change protected GitHub settings without explicit authorization.
