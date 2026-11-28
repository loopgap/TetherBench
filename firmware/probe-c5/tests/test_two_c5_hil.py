import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from hil_two_c5 import capture_and_verify


AP = (
    b"wifi:station: d0:cf:13:e7:8a:98 join, AID=1, ax, 20\n"
    b"esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.3\n"
)
STA = (
    b"wifi:connected with TetherBench-C5-912D58, aid = 1, channel 6, BW20(BELOW, C1)\n"
    b"wifi: c5-d0cf13e78a98 connected, IP=192.168.4.3, PHY=HE20\n"
)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, duration):
        assert duration >= 0
        self.now += duration


class FakePort:
    def __init__(self, owner):
        self.owner = owner
        self.data = b""
        self.closed = False

    def open(self):
        assert self.dtr is False and self.rts is False
        assert self.timeout == 0
        self.owner.opened.append(self.port)
        self.data = AP if self.port == "COM7" else STA

    def read(self, size):
        data, self.data = self.data[:size], self.data[size:]
        return data

    def close(self):
        self.closed = True


class FakeSerialModule:
    def __init__(self):
        self.opened = []
        self.ports = []

    def Serial(self):
        port = FakePort(self)
        self.ports.append(port)
        return port


class TwoC5HILTests(unittest.TestCase):
    def test_captures_both_ports_without_reset_and_verifies_pair(self):
        clock = FakeClock()
        serial = FakeSerialModule()
        with tempfile.TemporaryDirectory() as output:
            result = capture_and_verify(
                "COM7",
                "COM8",
                seconds=1,
                output_dir=output,
                serial_module=serial,
                monotonic=clock.monotonic,
                sleep=clock.sleep,
                timestamp="test",
            )
            self.assertEqual(result["result"], "passed")
            self.assertEqual(serial.opened, ["COM7", "COM8"])
            self.assertTrue(all(port.closed for port in serial.ports))
            self.assertEqual(pathlib.Path(result["logs"]["ap"]).read_bytes(), AP)
            self.assertEqual(pathlib.Path(result["logs"]["sta"]).read_bytes(), STA)

    def test_rejects_invalid_duration_or_duplicate_port_before_open(self):
        serial = FakeSerialModule()
        with tempfile.TemporaryDirectory() as output:
            for args in (("COM7", "COM8", 0), ("COM7", "COM7", 1)):
                with self.subTest(args=args), self.assertRaises(ValueError):
                    capture_and_verify(*args, output_dir=output, serial_module=serial)
        self.assertEqual(serial.opened, [])

    def test_reads_past_deadline_do_not_sleep_for_negative_time(self):
        clock = FakeClock()
        serial = FakeSerialModule()
        serial_factory = serial.Serial

        def slow_serial():
            port = serial_factory()
            original_read = port.read

            def slow_read(size):
                clock.now += 0.6
                return original_read(size)

            port.read = slow_read
            return port

        serial.Serial = slow_serial
        with tempfile.TemporaryDirectory() as output:
            result = capture_and_verify(
                "COM7", "COM8", seconds=1, output_dir=output,
                serial_module=serial, monotonic=clock.monotonic, sleep=clock.sleep,
                timestamp="deadline",
            )
        self.assertEqual(result["result"], "passed")
        self.assertTrue(all(port.closed for port in serial.ports))

    def test_second_port_open_failure_closes_both_ports(self):
        serial = FakeSerialModule()
        serial_factory = serial.Serial

        def failing_serial():
            port = serial_factory()
            if len(serial.ports) == 2:
                def fail_open():
                    raise OSError("COM8 unavailable")
                port.open = fail_open
            return port

        serial.Serial = failing_serial
        with tempfile.TemporaryDirectory() as output:
            with self.assertRaisesRegex(OSError, "COM8 unavailable"):
                capture_and_verify("COM7", "COM8", output_dir=output, serial_module=serial)
        self.assertEqual(serial.opened, ["COM7"])
        self.assertTrue(all(port.closed for port in serial.ports))

    def test_empty_capture_fails_and_keeps_both_logs(self):
        serial = FakeSerialModule()
        serial_factory = serial.Serial

        def empty_serial():
            port = serial_factory()
            original_open = port.open

            def open_empty():
                original_open()
                port.data = b""

            port.open = open_empty
            return port

        serial.Serial = empty_serial
        clock = FakeClock()
        with tempfile.TemporaryDirectory() as output:
            with self.assertRaises(RuntimeError):
                capture_and_verify(
                    "COM7", "COM8", seconds=1, output_dir=output,
                    serial_module=serial, monotonic=clock.monotonic, sleep=clock.sleep,
                    timestamp="empty",
                )
            self.assertEqual((pathlib.Path(output) / "two-c5-empty-ap.log").read_bytes(), b"")
            self.assertEqual((pathlib.Path(output) / "two-c5-empty-sta.log").read_bytes(), b"")
        self.assertTrue(all(port.closed for port in serial.ports))

    def test_failed_link_keeps_logs_without_a_pass_result(self):
        serial = FakeSerialModule()
        serial_factory = serial.Serial

        def mismatched_serial():
            port = serial_factory()
            original_open = port.open

            def open_with_mismatch():
                original_open()
                if port.port == "COM8":
                    port.data = STA.replace(b"d0cf13e78a98", b"d0cf13e78a99")

            port.open = open_with_mismatch
            return port

        serial.Serial = mismatched_serial
        clock = FakeClock()
        with tempfile.TemporaryDirectory() as output:
            with self.assertRaisesRegex(RuntimeError, "AP saw"):
                capture_and_verify(
                    "COM7",
                    "COM8",
                    seconds=1,
                    output_dir=output,
                    serial_module=serial,
                    monotonic=clock.monotonic,
                    sleep=clock.sleep,
                    timestamp="failure",
                )
            self.assertTrue((pathlib.Path(output) / "two-c5-failure-ap.log").exists())
            self.assertFalse(list(pathlib.Path(output).glob("*.json")))
        self.assertTrue(all(port.closed for port in serial.ports))


if __name__ == "__main__":
    unittest.main()
