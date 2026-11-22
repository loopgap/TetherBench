# Probe C5 Rules

- This project owns the standalone ESP32-C5 probe firmware. Do not depend on Bridge P4 or Hosted C5 private source.
- Keep board pin assignments in an identified board profile; do not invent pins from the chip name.
- Keep real-time SWD/JTAG, UART, and reset operations local to the C5.
- Preserve explicit C/C++ linkage and component dependency declarations.
- Build with the documented ESP-IDF version when installed; report unavailable hardware separately from a successful build.
