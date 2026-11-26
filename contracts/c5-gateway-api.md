# Local C5 Gateway API

Status: implemented and covered by host tests; wireless hardware acceptance is pending. This API is separate from both the simulator API and the C5 firmware RPC. The firmware wire contract is [Probe C5](probe-c5.md).

## Boundary and limits

The prototype Gateway listens on loopback only and has no user authentication. Do not expose it to a LAN or forward its port. Requests use same-origin JSON unless an endpoint below specifies another content type. JSON bodies are limited to 16 KiB. C5 configuration accepts up to 16 named device profiles; responses never contain bearer tokens or private keys.

Successful operations return JSON. Errors use `{ "error": { "code": "...", "message": "..." } }`. Device path segments must be URL encoded. Each physical probe grants at most one live session. The session ID is a local bearer capability, not browser-bound user authentication; keep it inside the local workspace. It is renewed while the console is active and is separate from Gateway-to-C5 TLS certificate identity verification.

## Discovery and status

| Method and path | Behavior |
|---|---|
| `GET /api/v1/c5` | Return `{ devices, discoveryError, limits }`; configured devices are polled and unpaired discovery replies are not trusted. Limits currently report 16 devices, 4096-byte firmware payloads and 1024-byte bus transactions. |
| `POST /api/v1/c5/discover` | Refresh configured devices and return `{ devices, discoveryError }`. Request body must be empty. |
| `GET /api/v1/c5/{id}` | Return `{ info, ota, pins }` for a connected device. `pins` is ordered SWD, UART, SPI, I2C. |
| `GET /api/v1/c5/{id}/adapters` | Return locally active adapters, if any. |

Each list item has `id`, `name`, `transport`, `host`, `connected`, `info`, `busy`, `sessionActive`, `lastSeenAt`, `error`, `ota`, and `adapter`. A missing or untrusted device stays offline.

## Sessions and device operations

| Method and path | Request | Result |
|---|---|---|
| `POST /api/v1/c5/{id}/session` | Empty body | `201` with `{ id, deviceId, deadline }` |
| `GET /api/v1/c5/{id}/session/{sessionId}` | Empty body | Current session, or `409 session_expired` |
| `POST /api/v1/c5/{id}/session/{sessionId}/renew` | Empty body | Session with extended deadline |
| `DELETE /api/v1/c5/{id}/session/{sessionId}` | Empty body | Release ownership and active adapter |
| `PUT /api/v1/c5/{id}/pins` | `{ sessionId, protocol, hz, pins }` | `protocol`: 1 SWD, 2 UART, 3 SPI, 4 I2C; device validates available GPIO and conflicts. |
| `POST /api/v1/c5/{id}/dap` | `{ sessionId, data }` | `data` is even-length hexadecimal CMSIS-DAP bytes, at most 1024 bytes. Response contains hex `data`, `byteLength`, and `transactionMs`. |
| `POST /api/v1/c5/{id}/bus` | `{ sessionId, protocol, address?, data?, receive?, timeoutMs? }` | Hex `data` and `receive` are each limited to 1024 bytes; `timeoutMs` is 1..100. |
| `POST /api/v1/c5/{id}/wifi/scan` | `{ sessionId }` | Start or read the configured-SSID scan; the first request may return the firmware's busy status. |
| `POST /api/v1/c5/{id}/wifi/negotiate` | `{ sessionId, peerId, peerSessionId? }` | Coordinate five-sample Wi-Fi 6 measurements for two paired STA devices. The Gateway scans both devices, matches common candidate channels (the BSSIDs may differ across bands), verifies a ≥10-point worst-link improvement, reserves both devices, then prepares and commits together. If `peerSessionId` is absent, the Gateway acquires and releases a temporary peer lease. A failed/partial switch sends BAND_CANCEL to both and retries compensation while either device reconnects. |
| `POST /api/v1/c5/{id}/boot-confirm` or `/reboot` | `{ sessionId }` | Perform the named device operation. Reboot invalidates the session. |

Mutating calls require a live `sessionId`. One operation per device is active at a time. BUS and DAP results are not automatically replayed when the connection outcome is ambiguous. After a transport failure, the Gateway creates a fresh TLS client for the same discovered host and carries the existing lease value only so the session can renew if the C5 still recognizes it; an expired device lease invalidates the local session. The device lease, pin release rules, protocol payloads, and firmware timeouts are defined in [the firmware protocol](../firmware/probe-c5/docs/protocol.md).

Automatic channel switching currently requires both devices to be idle Wi-Fi 6 HE20 stations using the configured SSID, a common candidate channel seen in at least four of five scans on both ends, and no active protocol pins or adapters. Current-link RTT and loss come from five Gateway TLS INFO probes; candidate RSSI and missed scans come from each C5 radio. These are Gateway-to-device and C5-to-access-point observations, not direct C5-to-C5 packet-loss measurements. C5 AP+STA coordination and Windows hotspot channel control are not managed by this transaction.

## OTA and local adapters

| Method and path | Request | Behavior |
|---|---|---|
| `POST /api/v1/c5/{id}/ota` | `application/octet-stream`, `X-TetherBench-Session`, explicit `Content-Length`; 176-byte signed manifest followed by image | Streams bounded chunks to the inactive C5 slot and resumes from the device checkpoint. Request size is limited to the manifest plus one OTA slot. Response reports device validation state; it does not activate the image. |
| `POST /api/v1/c5/{id}/ota/activate` or `/ota/abort` | `{ sessionId, jobId }`; nonzero decimal u64 job ID | Activate or abort the named OTA job. Activation reboots and invalidates the session. |
| `POST /api/v1/c5/{id}/adapters` | `{ sessionId, type }` | Start one local adapter: `openocd`, `usb-dap`, `usb-uart`, `usb-spi`, or `usb-i2c`. Response includes loopback host, ephemeral port, and launch instructions. |
| `POST /api/v1/c5/{id}/adapters/stop` | `{ sessionId }` | Stop the adapter and release its socket. |

The OpenOCD adapter is a loopback CMSIS-DAP TCP relay. USB/IP protocol support does not establish Windows VHCI driver installation, device enumeration, or hardware functionality. The USB model uses laboratory VID/PID `1209:0001`; assign a project VID/PID before distributing it. See [debug backend status](../firmware/probe-c5/docs/debug-backends.md).

Discovery, bearer credentials, Wi-Fi, SWD waveforms, target identity and OTA rollback depend on hardware and must be verified independently of host route tests.
