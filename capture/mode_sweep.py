#!/usr/bin/env python3
"""Sweep 0x04 <main> 00 00 <aux> layouts, streaming 'byte i = i' so on-screen numbers name their byte offset.

  mode_sweep.py main 0-9 [aux] [hold]      # vary main display, fixed aux
  mode_sweep.py aux 0-5 [main] [hold]      # vary aux area, fixed main
"""
import sys, time
import usb.util
from mystique_image import open_device, command
from experiment import send_data

what = sys.argv[1]
lo, hi = map(int, sys.argv[2].split("-"))
fixed = int(sys.argv[3]) if len(sys.argv) > 3 else (0 if what == "main" else 5)
hold = float(sys.argv[4]) if len(sys.argv) > 4 else 15
FIELDS = {i: i for i in range(3, 42)}

dev = open_device()
try:
    command(dev, 0x03, b"\x01")
    for n in range(lo, hi + 1):
        main, aux = (n, fixed) if what == "main" else (fixed, n)
        print(time.strftime("%H:%M:%S"), f"0x04 main={main} aux={aux}:",
              command(dev, 0x04, bytes([main, 0, 0, aux])).hex()[:12], flush=True)
        end = time.monotonic() + hold
        while time.monotonic() < end:
            send_data(dev, FIELDS)
            time.sleep(1)
finally:
    usb.util.release_interface(dev, 0)
