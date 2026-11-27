# ADR 0004: Signed dual-slot updates for attached MCUs

Date: 2026-11-27. Status: accepted; C5 forwarding implementation present, target-side implementation and hardware verification pending.

## Decision

Use a TetherBench target bootloader contract over the C5's configured UART or SPI bus. The C5 accepts only targets that advertise dual-slot storage, independent signature and digest verification, durable resume, trial boot, rollback, a protected update agent, and a persistent anti-rollback index. C5 OTA remains a separate ESP-IDF flow.

The target bootloader owns its durable update journal and is the source of truth for candidate progress and boot result. The C5 verifies the signed target manifest before forwarding it and retains a small durable resource lock until the target reports a terminal state. The target verifies the same signature again before accepting any image. An update request never falls back to raw Flash access, a ROM loader, or SWD.

## Rationale

The existing C5 protocol can transfer bounded UART and SPI transactions but does not establish a target bootloader, image signature, inactive slot, resume checkpoint, trial confirmation, or rollback guarantee. Treating generic BUS transactions as OTA would leave those guarantees dependent on undocumented target behavior. A capability-gated protocol gives each target a concrete conformance boundary while keeping timing-sensitive target pins and bus work on the C5.

The detailed wire contract is [Target MCU OTA v1](../../contracts/target-mcu-ota.md). The C5 cross-project entry and current implementation status are in [Probe C5](../../contracts/probe-c5.md).

## Consequences

- The C5 RPC dispatcher implements target forwarding, signed-manifest preflight, target identity checks, and a durable recovery lock. This does not provide target-side firmware or establish target bootloader conformance.
- A target-side bootloader or protected update agent is required; a PC-side protocol alone cannot provide rollback or confirmation.
- Every supported board needs an identified Flash map, protected bootloader configuration, image format, electrical profile, and hardware validation.
- The 184-byte target manifest adds a signed rollback index without changing the existing 176-byte C5 manifest.
- C5 RPC opcode 40 carries one protocol byte followed by the 184-byte target manifest (185 request bytes total); target-side `BEGIN` still carries the manifest alone. Opcode 44 reads the locked job with an empty request or reads HELLO status using one UART/SPI protocol byte when no job is locked.
- Host tests may validate framing and recovery logic but cannot prove Flash atomicity, write protection, or electrical behavior.

## Open verification

No attached MCU target or board profile is present yet. STM32F405 Flash layout, write protection, reset entry, and target toolchain still require a concrete board and hardware evidence before the target adapter can advertise support.
