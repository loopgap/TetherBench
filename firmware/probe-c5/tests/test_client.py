import http.client
import pathlib
import ssl
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from client import Client, HEADER, INFO, RpcError, decode_response

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

if __name__ == "__main__":
    unittest.main()
