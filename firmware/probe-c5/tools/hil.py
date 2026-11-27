"""Exercise real C5 control without assigning or driving target pins."""
import argparse
import json
import pathlib
import struct
from client import (
    Client,
    RpcError,
    CONFIG,
    DAP,
    PINS,
    OTA_STATUS,
    ACQUIRE,
    RELEASE,
    REBOOT,
    ROOT,
)

def expect_status(client, status, op, payload=b"", **kwargs):
    try:
        client.rpc(op, payload, **kwargs)
    except RpcError as error:
        assert error.status == status, (op, error.status, status)
    else:
        raise AssertionError(f"opcode {op} unexpectedly succeeded")


def validate_preflight(info, config, ota_status):
    if (
        info.get("leaseActive") is not False
        or info.get("pendingBoot") is not False
        or info.get("bandStage") != 0
        or info.get("otaAvailable") is not True
    ):
        raise RuntimeError("HIL requires an idle lease/band state and a healthy C5 OTA service")
    if len(config) != 48:
        raise RuntimeError("HIL requires a complete 48-byte CONFIG response")
    for index in range(4):
        protocol, count, reserved, hz, *pins = struct.unpack_from("<BBHI4b", config, index * 12)
        if protocol != index + 1 or count != 0 or reserved != 0 or hz != 0 or pins != [-1] * 4:
            raise RuntimeError("HIL requires all four protocol pin maps to be unconfigured")
    if len(ota_status) != 28:
        raise RuntimeError("HIL requires a complete 28-byte C5 OTA status response")
    _, phase, _, _, _, pending_boot = struct.unpack("<QIIIII", ota_status)
    if phase not in (0, 4, 5) or pending_boot != 0:
        raise RuntimeError("HIL requires C5 OTA to be idle or terminal with no pending boot")


def preflight(client):
    info = client.info()
    if info["board"] != "esp32c5-n4" or info["protocol"] != 1:
        raise RuntimeError("Unexpected C5 identity or protocol")
    if not (info["wifi"]["ap"] or info["wifi"]["he20"] is True):
        raise RuntimeError("STA must negotiate HE20 before HIL")
    config = client.rpc(CONFIG, lease=0)
    ota_status = client.rpc(OTA_STATUS, lease=0)
    validate_preflight(info, config, ota_status)
    return info

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--credentials", type=pathlib.Path, default=ROOT / ".local/device.json")
    args = parser.parse_args()
    credentials = json.loads(args.credentials.read_text())
    client = Client(args.host, credentials, certificate=args.credentials.with_name("server.crt"))
    try:
        info = preflight(client)
        expect_status(client, 4, RELEASE, lease=0)
        client.acquire()
        expect_status(client, 3, ACQUIRE, struct.pack("<I", 30000), lease=0)
        expect_status(client, 4, DAP, b"\x00\xf0", lease=client.lease ^ 1)
        expect_status(client, 1, DAP, b"\x05")
        expect_status(client, 8, DAP, b"\x02\x01")
        response = client.rpc(DAP, b"\x00\xff")
        assert response[:2] == b"\x00\x02" and struct.unpack_from("<H", response, 2)[0] == 1024
        invalid_pins = struct.pack("<BBHI4b", 1, 2, 0, 1000000, 16, 17, -1, -1)
        expect_status(client, 9, PINS, invalid_pins)
        assert len(client.rpc(OTA_STATUS, lease=0)) == 28
        client.renew()
        client.release()
        expect_status(client, 4, REBOOT, lease=0)
        print(json.dumps({"result": "passed", "id": info["id"],
            "wifi": info["wifi"], "freeHeap": info["freeHeap"],
            "minHeap": info["minHeap"], "checks": ["identity", "lease", "exclusive-owner",
            "safe-preflight", "DAP-info", "unmapped-SWD", "reserved-pins", "OTA-status"]}, indent=2))
    finally:
        try:
            client.release()
        finally:
            client.close()

if __name__ == "__main__":
    main()
