"""Exercise real C5 control without assigning or driving target pins."""
import argparse
import json
import pathlib
import struct
from client import (Client, RpcError, CONFIG, DAP, PINS, OTA_STATUS, ACQUIRE, RELEASE, REBOOT, ROOT)

def expect_status(client, status, op, payload=b"", **kwargs):
    try:
        client.rpc(op, payload, **kwargs)
    except RpcError as error:
        assert error.status == status, (op, error.status, status)
    else:
        raise AssertionError(f"opcode {op} unexpectedly succeeded")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--credentials", type=pathlib.Path, default=ROOT / ".local/device.json")
    args = parser.parse_args()
    credentials = json.loads(args.credentials.read_text())
    client = Client(args.host, credentials, certificate=args.credentials.with_name("server.crt"))
    try:
        info = client.info()
        assert info["board"] == "esp32c5-n4" and info["protocol"] == 1
        assert info["wifi"]["ap"] or info["wifi"]["phy"] == 5, "STA must negotiate HE20"
        assert len(client.rpc(CONFIG, lease=0)) == 48
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
            "DAP-info", "unmapped-SWD", "reserved-pins", "OTA-status"]}, indent=2))
    finally:
        try:
            client.release()
        finally:
            client.close()

if __name__ == "__main__":
    main()
