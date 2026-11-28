import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from verify_two_c5_link import LinkVerificationError, verify_link


AP = "wifi:station: d0:cf:13:e7:8a:98 join, AID=1, ax, 20\nesp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.3\n"
STA = "wifi:connected with TetherBench-C5-912D58, aid = 1, channel 6, BW20(BELOW, C1)\nwifi: c5-d0cf13e78a98 connected, IP=192.168.4.3, PHY=HE20\n"


class TwoC5LinkTests(unittest.TestCase):
    def test_matches_mac_ip_and_he20(self):
        self.assertEqual(verify_link(AP, STA)["staIp"], "192.168.4.3")

    def test_rejects_mismatched_board(self):
        with self.assertRaises(LinkVerificationError):
            verify_link(AP, STA.replace("d0cf13e78a98", "d0cf13e78a99"))

    def test_rejects_disconnect_after_ready(self):
        with self.assertRaises(LinkVerificationError):
            verify_link(AP, STA + "wifi: Disconnected (reason=201)\n")


if __name__ == "__main__":
    unittest.main()
