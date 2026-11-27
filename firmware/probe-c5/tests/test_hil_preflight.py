import pathlib
import struct
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from client import CONFIG, OTA_STATUS
from hil import preflight, validate_preflight


def empty_config():
    return b"".join(
        struct.pack("<BBHI4b", protocol, 0, 0, 0, -1, -1, -1, -1)
        for protocol in range(1, 5)
    )


def safe_info():
    return {
        "board": "esp32c5-n4",
        "protocol": 1,
        "wifi": {"ap": True, "he20": None},
        "leaseActive": False,
        "pendingBoot": False,
        "bandStage": 0,
        "otaAvailable": True,
    }


class HilPreflightTests(unittest.TestCase):
    def test_accepts_idle_device_with_unconfigured_buses(self):
        ota = struct.pack("<QIIIII", 0, 0, 0, 0, 0, 0)
        validate_preflight(safe_info(), empty_config(), ota)

    def test_preflight_reads_status_before_any_lease_operation(self):
        client = Mock()
        client.info.return_value = safe_info()
        client.rpc.side_effect = [
            empty_config(),
            struct.pack("<QIIIII", 0, 0, 0, 0, 0, 0),
        ]

        preflight(client)

        self.assertEqual(client.rpc.call_args_list[0].args, (CONFIG,))
        self.assertEqual(client.rpc.call_args_list[0].kwargs, {"lease": 0})
        self.assertEqual(client.rpc.call_args_list[1].args, (OTA_STATUS,))
        self.assertEqual(client.rpc.call_args_list[1].kwargs, {"lease": 0})
        client.acquire.assert_not_called()
        client.release.assert_not_called()

    def test_rejects_nonempty_or_malformed_bus_configuration(self):
        ota = struct.pack("<QIIIII", 0, 0, 0, 0, 0, 0)
        configured = bytearray(empty_config())
        configured[1] = 2
        for config in (bytes(configured), empty_config()[:-1]):
            with self.subTest(config=config):
                with self.assertRaises(RuntimeError):
                    validate_preflight(safe_info(), config, ota)

    def test_rejects_unsafe_device_states(self):
        ota_idle = struct.pack("<QIIIII", 0, 0, 0, 0, 0, 0)
        states = (
            {"leaseActive": True},
            {"pendingBoot": True},
            {"bandStage": 1},
            {"otaAvailable": False},
        )
        for change in states:
            with self.subTest(change=change):
                info = safe_info()
                info.update(change)
                with self.assertRaises(RuntimeError):
                    validate_preflight(info, empty_config(), ota_idle)

    def test_rejects_nonterminal_ota_or_pending_boot(self):
        for phase, pending_boot in ((1, 0), (2, 0), (3, 0), (4, 1), (6, 0)):
            with self.subTest(phase=phase, pending_boot=pending_boot):
                ota = struct.pack("<QIIIII", 7, phase, 0, 0, 1, pending_boot)
                with self.assertRaises(RuntimeError):
                    validate_preflight(safe_info(), empty_config(), ota)


if __name__ == "__main__":
    unittest.main()
