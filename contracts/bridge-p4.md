# P4 USB Bridge Frame Contract

Status: version 1 frame format implemented in `firmware/bridge-p4`; the live COM10
PING/error smoke test passes after flashing the identified ESP32-P4 board. USB
High-Speed descriptors, CMSIS-DAP v2 bulk interoperability, COM9 full-speed
transport, and the P4-to-C5 physical link remain unverified.

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

The current host observation identifies COM10 as VID:PID `303A:4001` running a
factory-test image that reports `board=WT9932P4-TINY`, `pins=39`, and
`gpio_window=CLOSED`. This is provisional board identification only. It does not
identify the USB speed, expose a schematic GPIO map, or prove that the image is
the TetherBench bridge. COM9 currently identifies as VID:PID `303A:1001` and
reports `factory_test_dut`; it is not bridge evidence either. The captured
read-only evidence is kept in
[`firmware/bridge-p4/docs/validation.md`](../firmware/bridge-p4/docs/validation.md).

The ESP-IDF TinyUSB device stack binds one peripheral per image. The bridge
therefore uses explicit `high` and `full` build profiles to validate COM10 and
COM9 sequentially; a profile must not be described as simultaneous dual-port
service until a separate multi-stack implementation and descriptor evidence exist.

The P4 route still needs independent evidence for USB enumeration, High-Speed
throughput, CMSIS-DAP v2 interoperability, C5 transport, SWD transactions,
target-MCU OTA, interruption recovery, and power-loss recovery.

The provisional Wireless-Tag Tiny wiring profile is recorded in
[`contracts/wt-tiny-pin-profile.md`](wt-tiny-pin-profile.md). It uses P4
GPIO16..21 and C5 GPIO0/1/8/9/24 plus the C5 `EN` reset input. The profile is
not hardware acceptance evidence until the exact P4/P4X marking, reset levels,
SPI-slave implementation, and frame error rate have been checked on the two
boards.
