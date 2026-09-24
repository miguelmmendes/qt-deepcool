#!/usr/bin/env python3
"""Send control commands, then stream fake Machine Info values so we can see where each one lands.

  uv run --with pyusb --with pillow experiment.py [--cmd 07:0000 ...] [--seconds 30] [--set 3=11 6=22 ...]
"""
import sys, time
import usb.util
from mystique_image import open_device, command, sum16

EP_DATA_OUT, EP_DATA_IN = 0x02, 0x82
DEFAULT = {3: 11, 6: 22, 9: 33, 11: 44, 14: 55, 21: 6, 23: 66}


def data_packet(cmd, fields):
    pkt = bytearray(48)
    pkt[0:3] = bytes([0xAA, 0x2E, cmd])
    for off, val in fields.items():
        pkt[off] = val
    pkt[42:46] = b"HIDC"
    pkt[46:48] = sum16(pkt[:46]).to_bytes(2, "little")
    return bytes(pkt)


def send_data(dev, fields):
    for pkt in (data_packet(0x10, {}), data_packet(0x01, fields)):
        dev.write(EP_DATA_OUT, pkt, timeout=2000)
        dev.read(EP_DATA_IN, 64, timeout=2000)


def main():
    args = sys.argv[1:]
    cmds = [a for i, a in enumerate(args) if i and args[i - 1] == "--cmd"]
    seconds = float(args[args.index("--seconds") + 1]) if "--seconds" in args else 30
    fields = dict(DEFAULT)
    if "--set" in args:
        for kv in args[args.index("--set") + 1:]:
            if kv.startswith("--"):
                break
            k, v = kv.split("=")
            fields[int(k)] = int(v, 0)
    dev = open_device()
    try:
        for c in cmds:
            op, _, payload = c.partition(":")
            print(f"cmd {op} {payload}:", command(dev, int(op, 16), bytes.fromhex(payload)).hex()[:12])
        print("streaming", {k: v for k, v in sorted(fields.items())}, f"for {seconds}s")
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            send_data(dev, fields)
            time.sleep(1)
    finally:
        usb.util.release_interface(dev, 0)


if __name__ == "__main__":
    main()
