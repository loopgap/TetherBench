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
TARGET_INFO = 39
TARGET_OTA_BEGIN, TARGET_OTA_WRITE, TARGET_OTA_FINISH, TARGET_OTA_ACTIVATE, TARGET_OTA_STATUS, TARGET_OTA_ABORT = (
    range(40, 46)
)
WIFI_SCAN, BAND_PREPARE, BAND_COMMIT, BAND_CONFIRM, BAND_CANCEL = range(48, 53)
REBOOT = 63
UART, SPI = 2, 3
TARGET_MANIFEST_SIZE = 184
TARGET_HELLO_SIZE = 180
TARGET_STATUS_SIZE = 64
TARGET_WRITE_MAX = 983
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

    @staticmethod
    def _target_protocol(protocol):
        if type(protocol) is not int or protocol not in (UART, SPI):
            raise ValueError("Target protocol must be UART (2) or SPI (3)")
        return protocol

    @staticmethod
    def _target_job_id(job_id):
        if type(job_id) is not int or not 1 <= job_id <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("Target job ID must be a nonzero u64")
        return struct.pack("<Q", job_id)

    @staticmethod
    def _target_manifest(manifest):
        if not isinstance(manifest, (bytes, bytearray, memoryview)):
            raise ValueError("Target manifest must be bytes")
        manifest = bytes(manifest)
        if len(manifest) != TARGET_MANIFEST_SIZE:
            raise ValueError("Target manifest must be exactly 184 bytes")
        invalid_header = (
            not struct.unpack_from("<Q", manifest)[0]
            or manifest[8] != 1
            or manifest[9]
            or struct.unpack_from("<H", manifest, 10)[0]
            or not struct.unpack_from("<I", manifest, 12)[0]
            or not struct.unpack_from("<Q", manifest, 112)[0]
        )
        if invalid_header:
            raise ValueError("Invalid target manifest header")
        for offset in (48, 80):
            field = manifest[offset:offset + 32]
            terminator = field.find(b"\0")
            if terminator <= 0 or any(field[terminator + 1:]):
                raise ValueError("Target manifest strings must be NUL-terminated and zero-padded")
        return manifest

    def _target_rpc(self, op, payload, expected_size):
        response = self.rpc(op, payload)
        if len(response) != expected_size:
            raise ValueError("Invalid target update response length")
        return response

    def target_info(self, protocol):
        protocol = self._target_protocol(protocol)
        return self._target_rpc(TARGET_INFO, bytes((protocol,)), TARGET_HELLO_SIZE)

    def target_ota_begin(self, protocol, manifest):
        protocol = self._target_protocol(protocol)
        manifest = self._target_manifest(manifest)
        return self._target_rpc(TARGET_OTA_BEGIN, bytes((protocol,)) + manifest, TARGET_STATUS_SIZE)

    def target_ota_write(self, job_id, offset, data):
        job = self._target_job_id(job_id)
        if type(offset) is not int or not 0 <= offset <= 0xFFFFFFFF:
            raise ValueError("Target write offset must be a u32")
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise ValueError("Target write data must be bytes")
        data = bytes(data)
        if not 1 <= len(data) <= TARGET_WRITE_MAX or offset > 0xFFFFFFFF - len(data):
            raise ValueError("Target write must contain 1..983 bytes without u32 overflow")
        payload = job + struct.pack("<I", offset) + data
        return self._target_rpc(TARGET_OTA_WRITE, payload, TARGET_STATUS_SIZE)

    def target_ota_finish(self, job_id):
        return self._target_rpc(TARGET_OTA_FINISH, self._target_job_id(job_id), TARGET_STATUS_SIZE)

    def target_ota_activate(self, job_id):
        return self._target_rpc(TARGET_OTA_ACTIVATE, self._target_job_id(job_id), TARGET_STATUS_SIZE)

    def target_ota_status(self):
        return self._target_rpc(TARGET_OTA_STATUS, b"", TARGET_STATUS_SIZE)

    def target_ota_abort(self, job_id):
        return self._target_rpc(TARGET_OTA_ABORT, self._target_job_id(job_id), TARGET_STATUS_SIZE)

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
    client = Client(
        args.host, json.loads(args.credentials.read_text()), certificate=args.credentials.with_name("server.crt")
    )
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
