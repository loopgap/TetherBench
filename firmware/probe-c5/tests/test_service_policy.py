import pathlib
import re
import unittest


SERVICE = pathlib.Path(__file__).resolve().parents[1] / "components/probe_service/probe_service.c"


class ServicePolicyTests(unittest.TestCase):
    def test_p4_link_requires_explicit_profile_and_checks_spi_conflicts(self):
        source = SERVICE.read_text(encoding="utf-8")
        self.assertIn("tb_service_start_with_link(enum tb_probe_link_profile profile)", source)
        self.assertIn("TB_PROBE_LINK_PROFILE_DISABLED", source)
        self.assertRegex(source, r"tb_probe_link_active\(\).*pins\.protocol == TB_SPI")
        self.assertRegex(source, r"tb_probe_link_active\(\).*p\[0\] == TB_SPI")

    def test_start_failure_releases_partial_service_state(self):
        source = SERVICE.read_text(encoding="utf-8")
        self.assertIn("static void cleanup_start_failure(httpd_handle_t server)", source)
        self.assertRegex(
            source,
            r"if \(!mutex \|\| !rpc_mutex\) \{\s*release_service_mutexes\(\);",
        )
        self.assertRegex(
            source,
            r"if \(xTaskCreate\(discover,.*?\) != pdPASS\) \{\s*"
            r"vTaskDelete\(control_task\);\s*cleanup_start_failure\(server\);",
        )

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
