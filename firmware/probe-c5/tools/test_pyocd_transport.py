"""Exercise CMSIS-DAP TCP framing without installing pyOCD."""
import socketserver
import struct
import sys
import threading
from types import ModuleType
from unittest.mock import patch
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pyocd_probe import TcpInterface, register  # noqa: E402


class TcpInterfaceTests(unittest.TestCase):
    def test_probe_plugin_registers_only_named_local_endpoints(self):
        modules = {}
        for name in ("pyocd", "pyocd.probe", "pyocd.probe.pydapaccess"):
            modules[name] = ModuleType(name)
            modules[name].__path__ = []

        aggregator = ModuleType("pyocd.probe.aggregator")
        aggregator.PROBE_CLASSES = {}
        cmsis_dap_probe = ModuleType("pyocd.probe.cmsis_dap_probe")
        access_module = ModuleType("pyocd.probe.pydapaccess.dap_access_cmsis_dap")
        api_module = ModuleType("pyocd.probe.pydapaccess.dap_access_api")

        class Probe:
            def __init__(self, access): self.access = access

        class Access:
            def __init__(self, unique_id, *, interface): self.unique_id, self.interface = unique_id, interface

        class DeviceError(Exception): pass
        cmsis_dap_probe.CMSISDAPProbe = Probe
        access_module.DAPAccessCMSISDAP = Access
        api_module.DAPAccessIntf = type("DAPAccessIntf", (), {"DeviceError": DeviceError})
        modules.update({aggregator.__name__: aggregator, cmsis_dap_probe.__name__: cmsis_dap_probe,
            access_module.__name__: access_module, api_module.__name__: api_module})

        with patch.dict(sys.modules, modules):
            plugin = register({"c5-east": 31001, "c5-west": 31002})
            probes = plugin.get_all_connected_probes(unique_id="WEST")
            self.assertEqual(aggregator.PROBE_CLASSES["tetherbench"], plugin)
            self.assertEqual([probe.access.interface.get_serial_number() for probe in probes], ["c5-west"])
            self.assertEqual(plugin.get_probe_with_id("c5-east").access.interface.port, 31001)
            self.assertIsNone(plugin.get_probe_with_id("unknown"))
            with patch("pyocd_probe.socket.create_connection", side_effect=OSError("offline")):
                with self.assertRaisesRegex(DeviceError, "offline"):
                    probes[0].access.interface.open()

    def test_fragmented_response_and_single_inflight_packet(self):
        received = []

        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                header = self.rfile.read(8)
                signature, length, kind, reserved = struct.unpack("<IHBB", header)
                packet = self.rfile.read(length)
                received.append((signature, kind, reserved, packet))
                response = struct.pack("<IHBB", 0x00504144, 3, 2, 0) + b"\x00\x01\x02"
                self.wfile.write(response[:5])
                self.wfile.flush()
                self.wfile.write(response[5:])
                self.wfile.flush()

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        server = Server(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        interface = TcpInterface("probe-1", server.server_address[1])
        try:
            interface.open()
            interface.write(b"\x00\x01")
            with self.assertRaises(OSError):
                interface.write(b"\x03")
            self.assertEqual(interface.read(), b"\x00\x01\x02")
            self.assertEqual(received, [(0x00504144, 1, 0, b"\x00\x01")])
            self.assertFalse(interface.pending)
        finally:
            interface.close()
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)

    def test_rejects_read_without_pending_response(self):
        interface = TcpInterface("probe-1", 1)
        with self.assertRaisesRegex(OSError, "No CMSIS-DAP response is pending"):
            interface.read()

    def test_bad_response_closes_connection_and_clears_pending_state(self):
        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                _, length, _, _ = struct.unpack("<IHBB", self.rfile.read(8))
                self.rfile.read(length)
                self.wfile.write(struct.pack("<IHBB", 0, 1, 2, 0) + b"\x00")
                self.wfile.flush()

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True
            daemon_threads = True

        server = Server(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        interface = TcpInterface("probe-1", server.server_address[1])
        try:
            interface.open()
            interface.write(b"\x00")
            with self.assertRaisesRegex(OSError, "Invalid CMSIS-DAP TCP response"):
                interface.read()
            self.assertIsNone(interface.socket)
            self.assertFalse(interface.pending)
        finally:
            interface.close()
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
