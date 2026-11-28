# P4 bridge validation record

This record keeps P4 + C5 evidence separate from the pure C5 wireless route.
The two routes are independent acceptance paths for the same host-side Gateway.

## 2026-11-28 local observation

The Windows host enumerated these serial interfaces:

| Port | VID:PID | Read-only observation | Interpretation |
|---|---|---|---|
| COM9 | `303A:1001` | `factory_test_dut`; repeated TinyUSB CDC flush warnings | Existing factory-test image; no bridge frame evidence |
| COM10 | `303A:4001` | `WTFT/1 0 INFO board=WT9932P4-TINY pins=39 gpio_window=CLOSED` | Provisional board identity only |

The probe opened each port with DTR and RTS disabled, read for five seconds, sent
no bytes, and closed both handles. This confirms that both interfaces are present
and producing logs. It does not establish USB speed, USB descriptors, CMSIS-DAP
interoperability, P4-to-C5 wiring, or target SWD access. Port numbers are not a
hardware contract.

## Firmware build evidence

`tools/build.ps1 -ExtraComponentDirs C:\Temp\tb-p4-components` completed with
ESP-IDF `D:\esp-idf\v6.1.1\esp-idf` and produced the ESP32-P4 application and
bootloader images. The generated `build/`, `sdkconfig`, and local component lock
were removed after the check. This is a source/linkage check, not a flash or HIL
result.

## 2026-11-28 flash and frame smoke test

`esptool` connected to COM9 and identified an ESP32-P4 revision 3.2 with MAC
`80:f1:b2:d7:2b:b4` and a detected 16 MiB flash. The complete 16 MiB factory
image was saved before writing. The bootloader, partition table, and application
from commit `e786214` were written at `0x2000`, `0x8000`, and `0x10000`; each
segment reported a verified hash.

COM10 then answered the version 1 frame contract:

| Request | Response | Result |
|---|---|---|
| `PING`, sequence `0x4321`, one-byte payload `aa` | `PING`, sequence `0x4321`, length `1`, status `OK` | passed |
| `DAP_REQUEST`, sequence `0x4322` | `ERROR`, sequence `0x4322`, length `1`, status `LINK_OFFLINE` | fail-closed as designed |

Both responses were 32 bytes with valid CRC-16/IBM. COM9 produced no response in
a three-second request window, so the current image has no evidence of a working
full-speed bridge endpoint. The source now supports `tools/build.ps1 -UsbPort
full` and `-UsbPort high`; these profiles must be flashed and tested separately
because the ESP-IDF TinyUSB device stack installs one peripheral per image. The
The full-speed profile built and flashed with verified segment hashes, but COM9
still returned no PING and COM10 disappeared while that profile was installed.
The high-speed profile was then rebuilt, flashed, and rechecked: COM10 returned a
valid PING response again. The board is therefore left on the high-speed image;
COM9 requires a board-level FS/VBUS/connector investigation before it can be
accepted. The factory-test logs and the old image backup remain local and ignored.

## Required gates before selecting pins or flashing

1. Identify the exact WT9932P4-TINY board revision and schematic, then record
   reserved USB, flash, PSRAM, boot, and factory-test GPIOs in a named board
   profile.
2. Select a high-speed C5 transport only from that profile and the C5 board
   revision. Record signal direction, voltage, ground, maximum clock, and a
   recovery state before enabling the pins in firmware.
3. Flash this bridge image only to the intended P4 device, capture USB
   descriptors on COM9/COM10, and verify the fixed-frame PING response.
4. Add independent throughput, disconnect/reconnect, C5 transport, CMSIS-DAP v2,
   SWD, target-OTA, and power-loss recovery evidence. A pure C5 Wi-Fi result or
   the factory-test logs cannot satisfy these gates.
