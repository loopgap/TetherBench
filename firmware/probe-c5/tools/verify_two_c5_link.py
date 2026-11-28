"""Verify a direct C5 AP/STA link from bounded serial logs."""

import argparse
import json
import pathlib
import re


class LinkVerificationError(RuntimeError):
    pass


def _match(pattern, text, label):
    match = re.search(pattern, text, re.IGNORECASE)
    if not match:
        raise LinkVerificationError(f"missing {label}")
    return match


def _normalise_mac(value):
    value = value.replace(":", "").lower()
    if len(value) != 12 or not re.fullmatch(r"[0-9a-f]{12}", value):
        raise LinkVerificationError(f"invalid MAC {value!r}")
    return ":".join(value[index:index + 2] for index in range(0, 12, 2))


def verify_link(ap_log, sta_log):
    ap_join = _match(
        r"wifi:station:\s*([0-9a-f:]{17}) join, AID=\d+, ax, 20",
        ap_log,
        "AP 802.11ax/20 MHz station association",
    )
    ap_ip = _match(
        r"DHCP server assigned IP to a client, IP is:\s*(\d{1,3}(?:\.\d{1,3}){3})",
        ap_log,
        "AP DHCP lease",
    ).group(1)
    sta_ready = _match(
        r"c5-([0-9a-f]{12}) connected, IP=(\d{1,3}(?:\.\d{1,3}){3}), PHY=HE20",
        sta_log,
        "STA HE20/IP readiness",
    )
    sta_assoc = _match(
        r"connected with .*?channel\s+(\d+), BW20\(",
        sta_log,
        "STA channel/BW20 association",
    )
    sta_mac = _normalise_mac(sta_ready.group(1))
    ap_mac = _normalise_mac(ap_join.group(1))
    if ap_mac != sta_mac:
        raise LinkVerificationError(f"AP saw {ap_mac}, STA reported {sta_mac}")
    sta_ip = sta_ready.group(2)
    if ap_ip != sta_ip:
        raise LinkVerificationError(f"AP leased {ap_ip}, STA reported {sta_ip}")
    if re.search(r"Disconnected|reason=\d+", sta_log[sta_ready.end():], re.IGNORECASE):
        raise LinkVerificationError("STA disconnected after HE20 readiness")
    return {
        "staMac": sta_mac,
        "staIp": sta_ip,
        "channel": int(sta_assoc.group(1)),
        "bandwidth": "20MHz",
        "he20": True,
        "noDisconnectAfterReady": True,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ap-log", type=pathlib.Path, required=True)
    parser.add_argument("--sta-log", type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = verify_link(
        args.ap_log.read_text(encoding="utf-8", errors="replace"),
        args.sta_log.read_text(encoding="utf-8", errors="replace"),
    )
    print(json.dumps({"result": "passed", **result}, indent=2))


if __name__ == "__main__":
    main()
