"""Capture and verify a direct C5 AP/STA link using read-only serial ports."""

import argparse
from datetime import datetime, timezone
import json
import pathlib
import time

from verify_two_c5_link import verify_link

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / ".local"


def _open_read_only(serial_module, name):
    port = serial_module.Serial()
    port.baudrate = 115200
    port.timeout = 0
    port.dtr = False
    port.rts = False
    port.port = name
    try:
        port.open()
    except Exception:
        port.close()
        raise
    return port


def capture_and_verify(
    ap_port,
    sta_port,
    seconds=60,
    output_dir=DEFAULT_OUTPUT,
    *,
    serial_module=None,
    monotonic=time.monotonic,
    sleep=time.sleep,
    timestamp=None,
):
    if type(seconds) is not int or not 1 <= seconds <= 120:
        raise ValueError("seconds must be an integer from 1 through 120")
    if not ap_port.strip() or not sta_port.strip() or ap_port.casefold() == sta_port.casefold():
        raise ValueError("AP and STA ports must be distinct, non-empty serial port names")
    if serial_module is None:
        import serial as serial_module

    stamp = timestamp or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output_dir = pathlib.Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    ap_log = output_dir / f"two-c5-{stamp}-ap.log"
    sta_log = output_dir / f"two-c5-{stamp}-sta.log"
    ports = []
    try:
        ports.append(_open_read_only(serial_module, ap_port))
        ports.append(_open_read_only(serial_module, sta_port))
        with ap_log.open("wb") as ap_file, sta_log.open("wb") as sta_file:
            files = (ap_file, sta_file)
            deadline = monotonic() + seconds
            while True:
                now = monotonic()
                if now >= deadline:
                    break
                for port, output in zip(ports, files):
                    chunk = port.read(4096)
                    if chunk:
                        output.write(chunk)
                sleep(min(0.002, max(0.0, deadline - monotonic())))
    finally:
        for port in reversed(ports):
            port.close()

    result = verify_link(
        ap_log.read_text(encoding="utf-8", errors="replace"),
        sta_log.read_text(encoding="utf-8", errors="replace"),
    )
    return {
        "result": "passed",
        "apPort": ap_port,
        "staPort": sta_port,
        "durationSeconds": seconds,
        "logs": {"ap": str(ap_log), "sta": str(sta_log)},
        **result,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ap-port", default="COM7", help="C5 SoftAP serial port (default: COM7)")
    parser.add_argument("--sta-port", default="COM8", help="C5 STA serial port (default: COM8)")
    parser.add_argument("--seconds", type=int, default=60, help="capture duration, 1..120 (default: 60)")
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    try:
        result = capture_and_verify(args.ap_port, args.sta_port, args.seconds, args.output_dir)
    except Exception as error:
        print(json.dumps({"result": "failed", "error": str(error)}, indent=2))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
