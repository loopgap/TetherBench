# Target MCU OTA protocol v1

Date: 2026-11-27. Status: specified; no C5 transport adapter or target bootloader implements this contract yet.

This contract defines updates of an MCU connected to Probe C5 over the configured UART or SPI bus. It does not describe C5's own dual-slot ESP-IDF OTA, CMSIS-DAP/SWD programming, or arbitrary raw bus access. The C5 cross-project entry remains [contracts/probe-c5.md](probe-c5.md).

## Required target capabilities

An updating target must provide a bootloader or protected update agent that reports every required capability before C5 accepts a manifest:

| Bit | Capability | Required behavior |
|---:|---|---|
| 0 | Two image slots | Never erase or overwrite the confirmed image while receiving a candidate |
| 1 | Signature verification | Independently verify the target manifest with the provisioned update public key |
| 2 | Image digest | Recompute SHA-256 from stored candidate bytes before activation |
| 3 | Durable resume | Persist the job, phase, and an aligned checkpoint before acknowledging progress |
| 4 | Trial boot | Boot a verified candidate without replacing the confirmed slot |
| 5 | Rollback | Restore the last confirmed image when trial confirmation misses its deadline |
| 6 | Protected updater | Prevent an untrusted application update from replacing or changing the trust anchor in the updater |
| 7 | Anti-rollback counter | Persist the highest confirmed signed rollback index and reject lower or equal indices |

If any required bit is absent, the target reports `UNSUPPORTED`; C5 must not fall back to ROM programming, raw writes, or SWD. Board-specific Flash layout, protection configuration, reset entry, voltage limits, and image format must be documented and independently verified for each target family.

The C5 authenticates the host over its pinned TLS connection and verifies the signed manifest before forwarding it. The target verifies the same signature and digest itself. CRC-32 detects link corruption and is not an authenticity check. The target bootloader is authoritative for durable progress, active slot, trial result, rollback, and confirmation. C5 keeps a small durable lock record so a C5 reboot cannot silently reopen SWD or raw bus operations during an unfinished update.

## Target manifest

The existing 176-byte manifest with `target=0` remains reserved for C5 firmware. Target MCU updates use a 184-byte signed manifest with `target=1`:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Nonzero `job_id` |
| 8 | 1 | `target=1` |
| 9 | 1 | `flags=0` |
| 10 | 2 | Reserved, zero |
| 12 | 4 | Image length, bounded by the target's reported slot capacity |
| 16 | 32 | SHA-256 of the complete image |
| 48 | 32 | NUL-terminated target firmware version, zero-padded |
| 80 | 32 | NUL-terminated target `board_id`, zero-padded |
| 112 | 8 | Unsigned rollback index, little-endian |
| 120 | 64 | ECDSA P-256 signature: big-endian `r` followed by big-endian `s` |

The signature covers bytes 0..119 using SHA-256. The target rejects a manifest whose board ID differs from its own, whose rollback index is not greater than the highest confirmed index, whose length exceeds either slot, or whose signature or image digest is invalid. Confirmation advances the durable rollback index; a failed trial does not.

## Physical frame

Integers are little-endian. One decoded request or response contains a 16-byte header, up to 997 payload bytes, a 4-byte CRC-32, then zero padding as needed for SPI:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `TBUP` |
| 4 | 1 | Frame version, 1 |
| 5 | 1 | Opcode |
| 6 | 1 | Status; zero in requests |
| 7 | 1 | Flags, zero |
| 8 | 4 | Nonzero sequence number |
| 12 | 2 | Payload length, 0..997 |
| 14 | 2 | Reserved, zero |
| 16 | variable | Payload |
| 16+length | 4 | IEEE CRC-32 of header and payload |

UART uses COBS encoding followed by a zero delimiter; a decoded packet is at most 1017 bytes, so the encoded packet including delimiter fits within the 1024-byte C5 bus limit. The receiver discards an invalid COBS packet through its delimiter and bounds each response by a 100 ms timeout. SPI uses mode 0 and one 1024-byte full-duplex transaction per poll; bytes after the CRC through byte 1023 must be zero. SPI is pipelined: each transaction returns the previous request's response. C5 sends `POLL` for the outstanding sequence until it receives the matching final response or the same bounded deadline expires. Only one request may be outstanding.

The target caches the last completed response. Repeating the same sequence and exact request returns that response; reusing a sequence with different contents returns `CONFLICT`. SPI `POLL` uses the outstanding sequence and is excluded from this duplicate rule; it returns `BUSY` while processing and the cached final response when ready. `WRITE` is additionally idempotent by `(job_id, offset, bytes)`: a duplicate within already written data succeeds only after readback matches; a conflicting duplicate fails. A reconnecting C5 queries `STATUS` before retrying an ambiguous operation.

| Opcode | Name | Request payload | Response payload |
|---:|---|---|---|
| 0 | POLL | Empty | Pending or response for the requested sequence |
| 1 | HELLO | Empty | 180-byte capability, identity, and update status record |
| 2 | BEGIN | 184-byte target manifest | 64-byte update status |
| 3 | WRITE | `job_id` u64, `offset` u32, `length` u16, 1..983 data bytes | 64-byte update status |
| 4 | FINISH | `job_id` u64 | 64-byte update status |
| 5 | ACTIVATE | `job_id` u64 | 64-byte update status |
| 6 | STATUS | `job_id` u64; zero means current job | 64-byte update status |
| 7 | ABORT | `job_id` u64 | 64-byte update status |

HELLO's first 116 bytes contain protocol major/minor u16, capability flags u32, maximum image length u32, maximum data chunk u16, reserved u16, `board_id[32]`, active version `[32]`, bootloader version `[16]`, unique ID `[12]`, and highest confirmed rollback index u64. The remaining 64 bytes are the current update status. Fixed strings must contain a NUL and have zero-filled trailing bytes. A target with no job returns an all-zero status record.

The 64-byte status contains `job_id` u64, phase u8, boot result u8, reserved u16, received u32, durable checkpoint u32, total u32, confirmed rollback index u64, and active version `[32]`. Phases are `EMPTY=0`, `RECEIVING=1`, `VERIFIED=2`, `TRIAL=3`, `CONFIRMED=4`, `ABORTED=5`, `ROLLED_BACK=6`, `FAILED=7`. Boot results are `NONE=0`, `TRIAL_PENDING=1`, `CONFIRMED=2`, `ROLLED_BACK=3`. Unknown status, phase, flag, or capability bits fail closed.

Target response status codes are `OK=0`, `INVALID=1`, `BUSY=2`, `UNSUPPORTED=3`, `IO=4`, `INTEGRITY=5`, `STATE=6`, `CONFLICT=7`, and `TIMEOUT=8`. C5 maps these to its corresponding RPC status and never converts a target error or timeout to success.

## Recovery and resource ownership

`BEGIN` durably records the signed manifest before erasing the inactive slot. `WRITE` is strictly sequential; the target acknowledges only bytes written and durably checkpointed. On reset, the target reports its checkpoint; the host resends the same manifest and resumes from that checkpoint. `FINISH` reads the candidate back, verifies its length, format, digest, and signature, then records `VERIFIED`. `ACTIVATE` records a trial boot before resetting the target. The candidate must confirm through the protected update agent before a target-defined bounded deadline; otherwise the bootloader restores the prior confirmed slot and records `ROLLED_BACK`. `ABORT` is valid only before trial activation and must durably clear the update intent.

Only one target update may exist on a C5 at a time. The C5 lease owns the selected UART or SPI pins. While its durable target-update lock is set, C5 rejects DAP, raw BUS, band switching, C5 OTA, and configuration of other protocols; it permits status, resume, same-protocol pin reconfiguration, and a confirmed pre-activation abort. Releasing a host lease releases electrical drivers but does not clear either update journal. C5 clears its lock only after target status confirms `CONFIRMED`, `ABORTED`, or `ROLLED_BACK`; communication errors leave the lock set.

No target firmware, supported board profile, bootloader protection evidence, or UART/SPI hardware test currently exists in this repository. Until those are added and verified, C5 reports no target-OTA capability and returns `UNSUPPORTED`; this specification does not claim external MCU updates are implemented.
