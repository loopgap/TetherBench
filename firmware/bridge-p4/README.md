# ESP32-P4 USB bridge

This is the independent P4 route: COM9 is reserved for full-speed USB and COM10 for the board's high-speed USB path. The current firmware exposes a bounded CDC framing endpoint so host tooling can validate framing before the board profile is identified. A later board profile will bind the same frame queue to a TinyUSB vendor bulk interface and the C5 transport.

The 32-byte little-endian frame is fixed-size: version (1), opcode (1), sequence (2), payload length (2), payload (24), and CRC-16/IBM (2). Payload lengths above 24, invalid versions, bad CRC, and queue exhaustion fail closed. PING is the only operation answered without a C5 link. DAP requests remain offline until an identified P4 board has a verified C5 SPI/SDIO wiring profile.

ESP-IDF 6.1.1 follows the official `espressif/esp_tinyusb` `^2.0.1~1` component range. Build with `tools/build.ps1` after activating the local IDF environment; generated build output stays ignored. The CDC endpoint is a framing and smoke-test path; it is not a claim of USB High-Speed enumeration or CMSIS-DAP hardware support. Those require an identified P4 board profile and a captured USB descriptor/throughput result.

The current COM10 read-only probe identifies a factory-test image for `WT9932P4-TINY` and reports 39 GPIOs with its GPIO window closed. Keep that as a provisional board profile only; do not select C5 transport pins from the count or from COM9/COM10 numbering. See [`docs/validation.md`](docs/validation.md) for the evidence and the remaining hardware gates.
