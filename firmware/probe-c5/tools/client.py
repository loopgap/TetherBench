"""Bounded C5 RPC client with exact provisioned TLS certificate matching."""
import argparse
import http.client
import json
import pathlib
import secrets
import socket
import ssl
import struct
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = struct.Struct("<4sBBHIIQ")
MAX_PAYLOAD = 4096
INFO, ACQUIRE, RENEW, RELEASE, PINS, CONFIG = range(1, 7)
DAP, BUS = 16, 17
OTA_BEGIN, OTA_WRITE, OTA_FINISH, OTA_ACTIVATE, OTA_STATUS, OTA_ABORT, BOOT_CONFIRM = range(32, 39)
WIFI_SCAN, BAND_PREPARE, BAND_COMMIT, BAND_CONFIRM, BAND_CANCEL = range(48, 53)
REBOOT = 63
STATUS = ("OK", "INVALID", "UNAUTHORIZED", "BUSY", "LEASE", "UNSUPPORTED",
          "IO", "INTEGRITY", "STATE", "CONFLICT", "TIMEOUT")

class RpcError(RuntimeError):
    def __init__(self, status, data=b""):
        self.status, self.data = status, data
        super().__init__(STATUS[status] if status < len(STATUS) else f"status {status}")

def decode_response(data, op, request_id, lease):
    if len(data) < HEADER.size:
        raise ValueError("Truncated C5 response")
    magic, version, reply_op, flags, reply_id, size, reply_lease = HEADER.unpack_from(data)
    if (magic != b"TBC5" or version != 1 or reply_op != op or reply_id != request_id
            or reply_lease != lease or not flags & 0x8000 or flags & 0x7F00
            or size > MAX_PAYLOAD or len(data) != HEADER.size + size):
        raise ValueError("Invalid C5 response")
    status, payload = flags & 255, data[HEADER.size:]
    if status:
        raise RpcError(status, payload)
    return payload

class Client:
    def __init__(self, host, credentials, port=443, timeout=5, certificate=None):
        self.host, self.port, self.timeout = host, port, timeout
        self.credentials = credentials
        if len(credentials["token"]) != 64 or len(bytes.fromhex(credentials["token"])) != 32:
            raise ValueError("Invalid bearer token")
        pem = pathlib.Path(certificate or ROOT / ".local/server.crt").read_text()
        self.certificate = ssl.PEM_cert_to_DER_cert(pem)
        self.connection = None
        self.lease = 0
        self.request_id = secrets.randbits(32)
        self.lock = threading.Lock()

    def close(self):
        if self.connection:
            self.connection.close()
            self.connection = None

    def connect(self):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        connection = http.client.HTTPSConnection(self.host, self.port, timeout=self.timeout, context=context)
        try:
            connection.connect()
            if not secrets.compare_digest(connection.sock.getpeercert(binary_form=True), self.certificate):
                raise ssl.SSLCertVerificationError("C5 certificate does not match provisioned identity")
        except BaseException:
            connection.close()
            raise
        connection.auto_open = 0
        self.connection = connection

    def rpc(self, op, payload=b"", *, lease=None):
        if len(payload) > MAX_PAYLOAD or not 0 <= op <= 255:
            raise ValueError("C5 request exceeds protocol limits")
        with self.lock:
            self.request_id = (self.request_id + 1) & 0xFFFFFFFF
            owner = self.lease if lease is None else lease
            frame = HEADER.pack(b"TBC5", 1, op, 0, self.request_id, len(payload), owner) + payload
            try:
                if self.connection is None or self.connection.sock is None:
                    self.close()
                    self.connect()
                self.connection.request("POST", "/v1/rpc", frame, {
                    "Authorization": "Bearer " + self.credentials["token"],
                    "Content-Type": "application/octet-stream",
                })
                response = self.connection.getresponse()
                data = response.read(HEADER.size + MAX_PAYLOAD + 1)
                if response.status != 200:
                    self.close()
                    raise RuntimeError(f"C5 HTTP status {response.status}")
                if response.getheader("Content-Type", "").split(";")[0] != "application/octet-stream":
                    raise ValueError("Invalid C5 content type")
                return decode_response(data, op, self.request_id, owner)
            except (OSError, http.client.HTTPException, ValueError):
                self.close()
                # A lost DAP/BUS reply is ambiguous; never replay it automatically.
                raise

    def info(self):
        return json.loads(self.rpc(INFO, lease=0))

    def acquire(self, ttl=30000):
        data = self.rpc(ACQUIRE, struct.pack("<I", ttl), lease=0)
        if len(data) != 12:
            raise ValueError("Invalid lease response")
        self.lease, actual = struct.unpack("<QI", data)
        if not self.lease or actual != ttl:
            raise ValueError("Invalid lease grant")
        return self.lease

    def renew(self, ttl=30000):
        self.rpc(RENEW, struct.pack("<I", ttl))

    def release(self):
        if self.lease:
            self.rpc(RELEASE)
            self.lease = 0

def discover(address="192.168.137.255", timeout=2):
    nonce, found = secrets.token_bytes(8), {}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(timeout)
        sock.sendto(b"TBC5DISC" + nonce, (address, 4440))
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            sock.settimeout(max(0.01, end - time.monotonic()))
            try:
                data, peer = sock.recvfrom(125)
            except socket.timeout:
                break
            if len(data) != 124 or data[:16] != b"TBC5HERE" + nonce or data[42] != 1:
                continue
            found[peer[0]] = {"host": peer[0], "port": struct.unpack_from("<H", data, 40)[0],
                "id": data[16:40].split(b"\0")[0].decode("ascii", "replace"),
                "certificate_sha256": data[44:76].hex(),
                "version": data[76:108].split(b"\0")[0].decode("ascii", "replace")}
    return list(found.values())

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("discover", "info", "confirm"))
    parser.add_argument("--host")
    parser.add_argument("--credentials", type=pathlib.Path, default=ROOT / ".local/device.json")
    parser.add_argument("--broadcast", default="192.168.137.255")
    args = parser.parse_args()
    if args.command == "discover":
        print(json.dumps(discover(args.broadcast), indent=2))
        return
    if not args.host:
        parser.error("--host is required")
    client = Client(args.host, json.loads(args.credentials.read_text()), certificate=args.credentials.with_name("server.crt"))
    try:
        if args.command == "confirm":
            client.acquire()
            client.rpc(BOOT_CONFIRM)
            client.release()
        print(json.dumps(client.info(), indent=2))
    finally:
        client.close()

if __name__ == "__main__":
    main()
