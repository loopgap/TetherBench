# C5 Debug and USB Backends

Status: CMSIS-DAP command parsing and local TCP relay are host-tested. C5 radio, GPIO timing, attached target debugging, pyOCD package integration and USB/IP device enumeration have not passed hardware validation. The browser API is specified in [the local C5 Gateway contract](../../../contracts/c5-gateway-api.md).

## OpenOCD over CMSIS-DAP TCP

The Gateway creates one ephemeral loopback listener per configured C5. Select the device in the console, acquire its session, configure SWD pins, and start `OpenOCD / pyOCD TCP`. Only one debug adapter can own that C5 at a time; the adapter closes when stopped, the session expires, or the device disconnects. It must not be rebound to a LAN interface.

The console prints a configuration equivalent to:

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port <ephemeral-port>
cmsis-dap tcp min_timeout 5500
transport select swd
adapter speed 1000
```

Add the desired OpenOCD target configuration after this interface configuration. The speed shown is the configured initial 1 MHz clock; it is not a measured GPIO frequency. `DAP_Info` advertises one 1024-byte packet. The firmware accepts one serialized command, validates request and response bounds, and applies a 250 ms SWD operation deadline. WAIT and match retry fields accept their CMSIS-DAP 16-bit range; expiry is enforced during each SWD transaction.

Run the portable parser and transport checks with the host compiler and the Python environment already used for ESP-IDF:

```powershell
$env:CC='D:/mingw64/bin/gcc.exe'
& 'C:/Espressif/tools/python/v6.1.1/venv/Scripts/python.exe' firmware/probe-c5/tools/test_core.py
$env:OPENOCD='C:/Espressif/tools/openocd-esp32/v0.12.0-esp32-20260831/openocd-esp32/bin/openocd.exe'
node firmware/probe-c5/tools/test_debug_backend.mjs
```

The OpenOCD check compiled the pinned Arm parser with GPIO test stubs and observed 18 command/response exchanges. OpenOCD reached “Interface ready” and exited successfully. On this Windows build, its TCP driver also emitted repeated `WSAGetLastError==10035` errors while polling an empty socket. The check records successful initialization only; it does not establish a clean OpenOCD transport, live wireless behavior, GPIO timing, an attached SWD target, or real target debugging.

## pyOCD adapter

`tools/pyocd_probe.py` registers each active loopback relay with the pyOCD CMSIS-DAP backend and restricts endpoints to 16 named local instances. It targets pyOCD 0.46.1. When that version is installed into the chosen Python environment, use the command printed by the console, with the profile ID and ephemeral port it returned:

```powershell
python firmware/probe-c5/tools/pyocd_probe.py --endpoint <profile-id>=<ephemeral-port> -- list
```

The standard-library tests cover TCP framing and plugin registration, named endpoint filtering, and error translation using small pyOCD API test doubles. The local C5 debug environment also installs pyOCD 0.46.1; tools/test_pyocd_installed.py verifies real plugin loading plus a CMSIS-DAP open against a bounded local responder. This does not prove radio, GPIO timing, an attached target, or GDB-server operation. Use the same Python environment for the tool and the pyOCD package. The client uses one in-flight packet, exact-length reads, a six-second socket bound and no automatic reconnect or command replay.

Run the installed-package check with:

    & firmware/probe-c5/.local/debug-venv/Scripts/python.exe firmware/probe-c5/tools/test_pyocd_installed.py

## USB/IP models

USB/IP control, import, enumeration descriptors, bulk DAP, UART CDC ACM, and bounded SPI/I2C vendor transactions have host protocol tests. The listener binds to loopback. Actual Windows device enumeration needs a compatible USB/IP client/VHCI driver and separate target validation. The current descriptor VID/PID is for laboratory development only. UART CDC uses the C5 UART configuration; SPI and I2C are vendor protocols, not standard USB serial interfaces.

Do not describe a TCP CMSIS-DAP relay or a passing USB/IP packet test as a virtual USB device attached to Windows. Do not claim SWD or bus electrical behavior from the parser and model tests.
