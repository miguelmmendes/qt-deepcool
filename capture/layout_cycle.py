#!/usr/bin/env python3
"""Cycle stats layouts via 0x08 00 <n>, streaming 'byte i = i' so on-screen numbers name their byte offset."""
import sys, time
import usb.util
from mystique_image import open_device, command
from experiment import send_data

LAYOUTS = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else range(8)
HOLD = float(sys.argv[2]) if len(sys.argv) > 2 else 20
FIELDS = {i: i for i in range(3, 42)}

dev = open_device()
try:
    command(dev, 0x03, b"\x01")
    for n in LAYOUTS:
        print(time.strftime("%H:%M:%S"), f"layout 0x08 00 {n:02x}:", command(dev, 0x08, bytes([0, n])).hex()[:12], flush=True)
        end = time.monotonic() + HOLD
        while time.monotonic() < end:
            send_data(dev, FIELDS)
            time.sleep(1)
finally:
    usb.util.release_interface(dev, 0)
