"""Capture bounded serial evidence to ignored local storage."""
import argparse
import pathlib
import time
import serial

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / ".local/serial.log")
    args = parser.parse_args()
    if not 0 < args.seconds <= 120:
        parser.error("seconds must be in (0,120]")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port, args.output.open("wb") as output:
        if args.reset:
            port.dtr = False
            port.rts = True
            time.sleep(0.1)
            port.rts = False
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            data = port.read(4096)
            if data:
                output.write(data)
    print(f"Captured {args.output.stat().st_size} serial bytes to local evidence.")

if __name__ == "__main__":
    main()
