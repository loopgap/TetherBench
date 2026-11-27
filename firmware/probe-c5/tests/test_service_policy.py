import pathlib
import re
import unittest


SERVICE = pathlib.Path(__file__).resolve().parents[1] / "components/probe_service/probe_service.c"


class ServicePolicyTests(unittest.TestCase):
    def test_pin_changes_are_rejected_during_target_ota(self):
        source = SERVICE.read_text(encoding="utf-8")
        match = re.search(r"case TB_PINS:.*?case TB_DAP:", source, re.DOTALL)
        self.assertIsNotNone(match)
        self.assertRegex(match.group(), r"if \(tb_target_ota_busy\(\)\)\s*return TB_BUSY;")
        self.assertNotIn("pins.protocol != tb_target_ota_protocol()", match.group())

    def test_target_ota_status_is_not_cached(self):
        source = SERVICE.read_text(encoding="utf-8")
        match = re.search(
            r"if \(status == TB_OK && frame\.op != TB_INFO.*?\) \{",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        self.assertIn("frame.op != TB_TARGET_OTA_STATUS", match.group())


if __name__ == "__main__":
    unittest.main()
