#!/usr/bin/env python3
"""Capture the AI-Passport serial console into build/serial_tianji.log.

Survives USB unplug/replug: reopens the port and appends to the same log,
so a reboot or a lost port never ends the capture session.
"""
import os
import serial
import sys
import time

PORT_GLOB_DIR = "/dev"
OUT = "build/serial_tianji.log"


def find_port():
    for name in sorted(os.listdir(PORT_GLOB_DIR)):
        if name.startswith("cu.usbmodem"):
            return os.path.join(PORT_GLOB_DIR, name)
    return None


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "ab", buffering=0) as f:
        f.write(f"\n===== capture start {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n".encode())
        while True:
            port = find_port()
            if port is None:
                time.sleep(1)
                continue
            try:
                with serial.Serial(port, 115200, timeout=0.2) as ser:
                    ser.setDTR(False)
                    ser.setRTS(False)
                    f.write(f"\n===== attached {port} {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n".encode())
                    while True:
                        data = ser.read(4096)
                        if data:
                            f.write(data)
            except KeyboardInterrupt:
                raise
            except (serial.SerialException, OSError) as exc:
                f.write(
                    f"\n===== port lost {port} {time.strftime('%Y-%m-%d %H:%M:%S')}: {exc} =====\n".encode()
                )
                time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
