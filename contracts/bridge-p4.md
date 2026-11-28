# P4 USB Bridge Frame Contract

Status: version 1 frame format implemented in `firmware/bridge-p4`; USB High-Speed,
CMSIS-DAP v2 bulk enumeration, and the P4-to-C5 physical link remain unverified.

This contract is independent of the pure C5 wireless route. A host must not infer
that a successful C5 RPC or Wi-Fi test proves P4 USB behavior.

## Frame

Every frame is exactly 32 bytes and is little-endian:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | `version`, currently `1` |
| 1 | 1 | `opcode` |
| 2 | 2 | `sequence`, echoed by the response |
| 4 | 2 | `payload_length`, `0..24` |
| 6 | 24 | payload; bytes after `payload_length` are zero-filled |
| 30 | 2 | CRC-16/IBM over bytes `0..29`, initial `0xffff`, polynomial `0xa001` |

The receiver rejects a wrong version, a payload length greater than 24, or a bad
CRC before enqueueing the frame. The queue is bounded to four frames; queue
exhaustion is reported as `QUEUE_FULL` when a response can be emitted.

## Opcodes and status

`PING` (`3`) is the only operation that may complete without a C5 link. A valid
PING response echoes the sequence and returns status `OK` in payload byte zero.
Other operations are held behind the C5 transport and return `LINK_OFFLINE` while
that transport is not provisioned. `ERROR` (`0x7f`) carries one status byte.

Status values are `OK=0`, `BAD_VERSION=1`, `BAD_LENGTH=2`, `QUEUE_FULL=3`, and
`LINK_OFFLINE=4`. A malformed frame has no trusted sequence and may be answered
with `BAD_LENGTH`; hosts must not retry a non-idempotent request after a lost
response without first reconciling the target state.

## Hardware boundary

COM9 and COM10 identify host-visible serial interfaces only. Their USB speed and
board role are not inferred from the port numbers. SPI/SDIO pins, USB PHY pins,
and C5 target pins must come from a named board profile after the P4 and C5 board
revisions are identified. Until then the bridge leaves external transport pins
disabled and must fail closed.

The P4 route still needs independent evidence for USB enumeration, High-Speed
throughput, CMSIS-DAP v2 interoperability, C5 transport, SWD transactions,
target-MCU OTA, interruption recovery, and power-loss recovery.
