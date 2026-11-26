"""Verify the installed pyOCD 0.46.1 stack can open the C5 TCP adapter.

This uses a bounded local CMSIS-DAP responder. It verifies pyOCD integration,
not radio, GPIO timing, or an attached target MCU.
"""
import socketserver
import struct
import sys
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pyocd_probe import TcpInterface, register  # noqa: E402


class DapResponder(socketserver.BaseRequestHandler):
    def _read_exact(self, size):
        result = bytearray()
        while len(result) < size:
            chunk = self.request.recv(size - len(result))
            if not chunk:
                return None
            result.extend(chunk)
        return bytes(result)

    def handle(self):
        while True:
            header = self._read_exact(8)
            if header is None:
                return
            signature, length, kind, reserved = struct.unpack("<IHBB", header)
            body = self._read_exact(length)
            if signature != 0x00504144 or kind != 1 or reserved or body is None:
                return
            command = body[0]
            if command == 0x00 and len(body) >= 2:
                values = {
                    1: b"TetherBench\\0",
                    2: b"CMSIS-DAP TCP\\0",
                    3: b"loopback\\0",
                    4: b"2.1.0\\0",
                    5: b"TetherBench\\0",
                    6: b"ESP32-C5\\0",
                    7: b"TetherBench\\0",
                    8: b"ESP32-C5\\0",
                    0xF0: b"\\0",
                    0xFB: b"\\0",
                    0xFC: b"\\0",
                    0xFD: b"\\0",
                    0xFE: b"\\1",
                    0xFF: struct.pack("<H", 1024),
                }
                value = values.get(body[1], b"")
                response = bytes((0x00, len(value))) + value
            else:
                response = bytes((command, 0))
            self.request.sendall(struct.pack("<IHBB", 0x00504144, len(response), 2, 0) + response)


class DapServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class InstalledPyOcdTests(unittest.TestCase):
    def test_real_pyocd_opens_tcp_adapter(self):
        try:
            import pyocd
            from pyocd.probe.pydapaccess.dap_access_cmsis_dap import DAPAccessCMSISDAP
        except ImportError as error:
            self.skipTest("install pyocd==0.46.1 in the selected C5 environment: " + str(error))

        self.assertEqual(pyocd.__version__, "0.46.1")
        server = DapServer(("127.0.0.1", 0), DapResponder)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        interface = TcpInterface("loopback", server.server_address[1])
        access = DAPAccessCMSISDAP("loopback", interface=interface)
        try:
            plugin = register({"loopback": server.server_address[1]})
            self.assertEqual(plugin.get_probe_with_id("loopback").unique_id, "loopback")
            access.open()
            self.assertEqual(interface.get_packet_count(), 1)
            self.assertEqual(interface.get_packet_size(), 1024)
        finally:
            access.close()
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
