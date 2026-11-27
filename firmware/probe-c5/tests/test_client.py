import http.client
import pathlib
import ssl
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from client import (
    Client,
    HEADER,
    INFO,
    RpcError,
    SPI,
    TARGET_HELLO_SIZE,
    TARGET_INFO,
    TARGET_MANIFEST_SIZE,
    TARGET_OTA_ABORT,
    TARGET_OTA_ACTIVATE,
    TARGET_OTA_BEGIN,
    TARGET_OTA_FINISH,
    TARGET_OTA_STATUS,
    TARGET_OTA_WRITE,
    TARGET_STATUS_SIZE,
    UART,
    decode_response,
)


def target_manifest():
    manifest = bytearray(TARGET_MANIFEST_SIZE)
    struct.pack_into("<Q", manifest, 0, 7)
    manifest[8] = 1
    struct.pack_into("<I", manifest, 12, 1024)
    manifest[48:52] = b"v1\0\0"
    manifest[80:86] = b"board\0"
    struct.pack_into("<Q", manifest, 112, 1)
    return bytes(manifest)

class ClientTests(unittest.TestCase):
    def test_response_boundaries(self):
        good = HEADER.pack(b"TBC5", 1, INFO, 0x8000, 42, 2, 0) + b"{}"
        self.assertEqual(decode_response(good, INFO, 42, 0), b"{}")
        for size in range(len(good)):
            with self.assertRaises(ValueError):
                decode_response(good[:size], INFO, 42, 0)
        for offset in (0, 4, 5, 7, 8, 12, 16):
            changed = bytearray(good)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                decode_response(changed, INFO, 42, 0)
        bad = HEADER.pack(b"TBC5", 1, INFO, 0x8004, 42, 0, 0)
        with self.assertRaises(RpcError) as result:
            decode_response(bad, INFO, 42, 0)
        self.assertEqual(result.exception.status, 4)

    def test_authentication_precedes_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            cert = pathlib.Path(folder) / "server.crt"
            cert.write_text("-----BEGIN CERTIFICATE-----\nYWJj\n-----END CERTIFICATE-----\n")
            client = Client("127.0.0.1", {"token": "a" * 64}, certificate=cert)
            connection = Mock()
            connection.sock.getpeercert.return_value = b"wrong peer"
            with patch("http.client.HTTPSConnection", return_value=connection):
                with self.assertRaises(ssl.SSLCertVerificationError):
                    client.rpc(INFO)
            connection.request.assert_not_called()
            connection.close.assert_called_once()
            self.assertIsNone(client.connection)

    def test_lost_reply_is_not_replayed(self):
        with tempfile.TemporaryDirectory() as folder:
            cert = pathlib.Path(folder) / "server.crt"
            cert.write_text("-----BEGIN CERTIFICATE-----\nYWJj\n-----END CERTIFICATE-----\n")
            client = Client("127.0.0.1", {"token": "a" * 64}, certificate=cert)
            connection = Mock()
            connection.sock.getpeercert.return_value = b"abc"
            connection.getresponse.side_effect = http.client.RemoteDisconnected()
            with patch("http.client.HTTPSConnection", return_value=connection):
                with self.assertRaises(http.client.RemoteDisconnected):
                    client.rpc(16, b"\x00\xf0")
            connection.request.assert_called_once()
            self.assertEqual(connection.auto_open, 0)
            self.assertIsNone(client.connection)

    def test_target_rpc_payloads(self):
        client = Client.__new__(Client)
        hello, status = bytes(TARGET_HELLO_SIZE), bytes(TARGET_STATUS_SIZE)
        client.rpc = Mock(side_effect=[hello, status, status, status, status, status, status, status])
        manifest = target_manifest()

        self.assertEqual(client.target_info(SPI), hello)
        self.assertEqual(client.target_ota_begin(UART, manifest), status)
        self.assertEqual(client.target_ota_write(0x0102030405060708, 9, b"abc"), status)
        self.assertEqual(client.target_ota_finish(7), status)
        self.assertEqual(client.target_ota_activate(7), status)
        self.assertEqual(client.target_ota_status(), status)
        self.assertEqual(client.target_ota_status(SPI), status)
        self.assertEqual(client.target_ota_abort(7), status)
        self.assertEqual(client.rpc.call_args_list, [
            ((TARGET_INFO, b"\x03"),),
            ((TARGET_OTA_BEGIN, b"\x02" + manifest),),
            ((TARGET_OTA_WRITE, struct.pack("<QI", 0x0102030405060708, 9) + b"abc"),),
            ((TARGET_OTA_FINISH, struct.pack("<Q", 7)),),
            ((TARGET_OTA_ACTIVATE, struct.pack("<Q", 7)),),
            ((TARGET_OTA_STATUS, b""),),
            ((TARGET_OTA_STATUS, b"\x03"),),
            ((TARGET_OTA_ABORT, struct.pack("<Q", 7)),),
        ])

    def test_target_rpc_rejects_invalid_requests_before_io(self):
        client = Client.__new__(Client)
        client.rpc = Mock(return_value=bytes(TARGET_STATUS_SIZE))
        manifest = target_manifest()
        invalid_manifests = [b"", bytes(TARGET_MANIFEST_SIZE - 1), bytes(TARGET_MANIFEST_SIZE + 1)]
        bad_target = bytearray(manifest)
        bad_target[8] = 0
        invalid_manifests.append(bytes(bad_target))
        bad_padding = bytearray(manifest)
        bad_padding[53] = 1
        invalid_manifests.append(bytes(bad_padding))

        for protocol in (0, 4, True):
            with self.subTest(protocol=protocol), self.assertRaises(ValueError):
                client.target_info(protocol)
            with self.subTest(status_protocol=protocol), self.assertRaises(ValueError):
                client.target_ota_status(protocol)
        for bad_manifest in invalid_manifests:
            with self.subTest(manifest_size=len(bad_manifest)), self.assertRaises(ValueError):
                client.target_ota_begin(UART, bad_manifest)
        for job_id in (0, -1, 1 << 64, True):
            with self.subTest(job_id=job_id), self.assertRaises(ValueError):
                client.target_ota_finish(job_id)
        invalid_writes = (
            (-1, b"x"),
            (1 << 32, b"x"),
            (False, b"x"),
            (0, b""),
            (0, bytes(984)),
            (0, "text"),
            (0xFFFFFFFF, b"x"),
        )
        for offset, data in invalid_writes:
            with self.subTest(offset=offset, data_length=len(data) if hasattr(data, "__len__") else None):
                with self.assertRaises(ValueError):
                    client.target_ota_write(1, offset, data)
        client.rpc.assert_not_called()

    def test_target_rpc_rejects_malformed_response_sizes(self):
        client = Client.__new__(Client)
        client.rpc = Mock(return_value=bytes(TARGET_HELLO_SIZE - 1))
        with self.assertRaises(ValueError):
            client.target_info(UART)
        client.rpc.return_value = bytes(TARGET_STATUS_SIZE + 1)
        with self.assertRaises(ValueError):
            client.target_ota_status()
        self.assertEqual(client.rpc.call_count, 2)

    def test_target_rpc_does_not_replay_lost_reply(self):
        client = Client.__new__(Client)
        client.rpc = Mock(side_effect=http.client.RemoteDisconnected())
        with self.assertRaises(http.client.RemoteDisconnected):
            client.target_ota_write(1, 0, b"image")
        client.rpc.assert_called_once_with(TARGET_OTA_WRITE, struct.pack("<QI", 1, 0) + b"image")

if __name__ == "__main__":
    unittest.main()
