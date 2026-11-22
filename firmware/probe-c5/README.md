# Probe C5

This is a minimal ESP-IDF C/C++ build skeleton, not a probe implementation. It has no board pin map, Wi-Fi configuration, CMSIS-DAP engine, target power control, or OTA code.

Activate an ESP-IDF v6.1.1 environment with ESP32-C5 support, then run:

```powershell
idf.py set-target esp32c5
idf.py build
```

The project intentionally keeps C `probe.c` separate from the C++ `app_main.cpp`; the public C function has explicit C linkage. No SDK was installed for this initialization, so compilation remains unverified. Add board pins and components only after the actual probe board is identified.
