"""Run pyOCD through the local CMSIS-DAP TCP adapter; no USB driver required."""
import argparse
import re
import socket
import struct
import sys


class TcpInterface:
    """Single in-flight packet, bounded reads, and no implicit reconnect/replay."""
    vid = pid = 0
    vendor_name = "TetherBench"
    product_name = "CMSIS-DAP TCP"
    is_bulk = True
    has_swo_ep = False

    def __init__(self, identity, port):
        if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}", identity) or not 1 <= port <= 65535:
            raise ValueError("Invalid local adapter identity or port")
        self.serial_number, self.port = identity, port
        self.packet_count, self.packet_size = 1, 1024
        self.socket, self.pending = None, False

    def open(self):
        if self.socket is None:
            self.socket = socket.create_connection(("127.0.0.1", self.port), timeout=6)
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self):
        if self.socket is not None:
            self.socket.close()
        self.socket, self.pending = None, False

    def write(self, data):
        if self.socket is None or self.pending or not 1 <= len(data) <= 1024:
            raise OSError("Adapter is closed, busy, or packet is invalid")
        try:
            self.socket.sendall(struct.pack("<IHBB", 0x00504144, len(data), 1, 0) + bytes(data))
            self.pending = True
        except OSError:
            self.close()
            raise

    def _read_exact(self, count):
        result = bytearray()
        while len(result) < count:
            chunk = self.socket.recv(count - len(result))
            if not chunk:
                raise OSError("CMSIS-DAP adapter disconnected")
            result.extend(chunk)
        return result

    def read(self):
        if self.socket is None or not self.pending:
            raise OSError("No CMSIS-DAP response is pending")
        try:
            signature, count, kind, reserved = struct.unpack("<IHBB", self._read_exact(8))
            if signature != 0x00504144 or kind != 2 or reserved or not 1 <= count <= 1024:
                raise OSError("Invalid CMSIS-DAP TCP response")
            result = self._read_exact(count)
            self.pending = False
            return result
        except OSError:
            self.close()
            raise

    def get_packet_count(self): return self.packet_count
    def set_packet_count(self, count): self.packet_count = min(1, count)
    def get_packet_size(self): return self.packet_size
    def set_packet_size(self, size):
        if not 1 <= size <= 1024:
            raise ValueError("CMSIS-DAP packet size exceeds 1024")
        self.packet_size = size
    def get_serial_number(self): return self.serial_number


def register(endpoints):
    from pyocd.probe.aggregator import PROBE_CLASSES
    from pyocd.probe.cmsis_dap_probe import CMSISDAPProbe
    from pyocd.probe.pydapaccess.dap_access_cmsis_dap import DAPAccessCMSISDAP
    from pyocd.probe.pydapaccess.dap_access_api import DAPAccessIntf

    class PyOCDInterface(TcpInterface):
        def open(self):
            try: super().open()
            except OSError as error: raise DAPAccessIntf.DeviceError(str(error)) from error
        def write(self, data):
            try: super().write(data)
            except OSError as error: raise DAPAccessIntf.DeviceError(str(error)) from error
        def read(self):
            try: return super().read()
            except OSError as error: raise DAPAccessIntf.DeviceError(str(error)) from error

    class TetherBenchProbe(CMSISDAPProbe):
        @classmethod
        def get_all_connected_probes(cls, unique_id=None, is_explicit=False):
            return [cls(DAPAccessCMSISDAP(None, interface=PyOCDInterface(identity, port)))
                    for identity, port in endpoints.items() if unique_id is None or unique_id.lower() in identity.lower()]
        @classmethod
        def get_probe_with_id(cls, unique_id, is_explicit=False):
            if unique_id in endpoints:
                return cls(DAPAccessCMSISDAP(None, interface=PyOCDInterface(unique_id, endpoints[unique_id])))
            return None

    PROBE_CLASSES["tetherbench"] = TetherBenchProbe
    return TetherBenchProbe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", action="append", required=True, metavar="ID=PORT")
    parser.add_argument("pyocd_args", nargs=argparse.REMAINDER, help="pyOCD arguments after --")
    args = parser.parse_args()
    endpoints = {}
    for value in args.endpoint:
        identity, separator, port_text = value.partition("=")
        if not separator or not port_text.isdecimal() or identity in endpoints or len(endpoints) >= 16:
            parser.error("Endpoints must be distinct ID=PORT pairs, at most 16")
        port = int(port_text)
        TcpInterface(identity, port)
        endpoints[identity] = port
    try:
        import pyocd
        if pyocd.__version__ != "0.46.1":
            parser.error("This adapter is validated against pyOCD 0.46.1")
        register(endpoints)
        from pyocd.__main__ import PyOCDTool
    except ImportError as error:
        parser.exit(2, "pyOCD 0.46.1 is required in this Python environment: " + str(error) + chr(10))
    arguments = args.pyocd_args
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    return PyOCDTool().run(arguments or ["list"])


if __name__ == "__main__":
    sys.exit(main())
