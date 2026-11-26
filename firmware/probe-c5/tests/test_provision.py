import pathlib
import sys
import tempfile
import unittest
import contextlib
import io
import json

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import provision


class ProvisionTests(unittest.TestCase):
    def test_ap_credentials_are_generated_once_and_can_be_supplied(self):
        with tempfile.TemporaryDirectory() as folder:
            local = pathlib.Path(folder)
            generated = provision.ap_network({}, local)
            self.assertTrue(generated["ssid"].startswith("TetherBench-C5-"))
            self.assertTrue(8 <= len(generated["password"].encode()) <= 63)
            self.assertEqual(provision.ap_network({}, local), generated)

            supplied = provision.ap_network(
                {"ap_ssid": "probe-ap", "ap_password": "test-password"}, local
            )
            self.assertEqual(supplied, {"ssid": "probe-ap", "password": "test-password"})
            with self.assertRaises(SystemExit):
                provision.ap_network({"ap_ssid": "missing-password"}, local)

    def test_ap_provisioning_needs_no_upstream_and_persists_private_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            local = pathlib.Path(folder)
            with contextlib.redirect_stdout(io.StringIO()):
                provision.provision(local, use_ap=True)
            config = (local / "device_config.h").read_text(encoding="utf-8")
            credentials = json.loads((local / "ap-network.json").read_text(encoding="utf-8"))
            self.assertIn("#define TB_WIFI_AP 1", config)
            self.assertIn(",".join(str(value) for value in credentials["ssid"].encode()), config)
            self.assertIn(",".join(str(value) for value in credentials["password"].encode()), config)
            with contextlib.redirect_stdout(io.StringIO()):
                provision.provision(local, use_ap=True)
            self.assertEqual(json.loads((local / "ap-network.json").read_text(encoding="utf-8")), credentials)

    def test_wifi_credentials_enforce_byte_limits(self):
        self.assertEqual(provision.validate_wifi("c5", "12345678")["ssid"], "c5")
        for ssid, password in (("", "12345678"), ("x" * 33, "12345678"), ("c5", "short"), ("c5", "x" * 64)):
            with self.assertRaises(SystemExit):
                provision.validate_wifi(ssid, password)


if __name__ == "__main__":
    unittest.main()
