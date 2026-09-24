#!/usr/bin/env python3
"""Part A: sweep aux area 0-7 (main=CPU temp). Part B: set 0x15/0x16/0x17 labels, then show several layouts."""
import time
import usb.util
from mystique_image import open_device, command
from experiment import send_data

FIELDS = {i: i for i in range(3, 42)}


def hold(dev, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        send_data(dev, FIELDS)
        time.sleep(1)


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


dev = open_device()
try:
    command(dev, 0x03, b"\x01")
    for aux in range(8):
        log(f"A: main=5 aux={aux} " + command(dev, 0x04, bytes([5, 0, 0, aux])).hex()[:12])
        hold(dev, 12)
    for cmd, text in ((0x15, b"AB"), (0x16, b"CD"), (0x17, b"EF")):
        log(f"B: label {cmd:#04x} = {text.decode()} " + command(dev, cmd, text).hex()[:12])
    for main, aux in ((5, 0), (5, 1), (0, 1), (2, 0), (4, 1), (3, 0)):
        log(f"B: main={main} aux={aux} " + command(dev, 0x04, bytes([main, 0, 0, aux])).hex()[:12])
        hold(dev, 10)
    log("done")
finally:
    usb.util.release_interface(dev, 0)
