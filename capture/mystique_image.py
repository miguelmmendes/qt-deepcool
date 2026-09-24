#!/usr/bin/env python3
"""Experimental image uploader for the DeepCool MYSTIQUE 240/360 LCD (480x640).

Protocol reverse-engineered from DeepCreative USB captures (see PROTOCOL.md, "Image Upload").

Usage:
  uv run --with pyusb --with pillow mystique_image.py upload picture.png [--slot N]
  uv run --with pyusb --with pillow mystique_image.py test            # render + upload a test card
  uv run --with pyusb --with pillow mystique_image.py info            # back to the built-in stats screen
  uv run --with pyusb --with pillow mystique_image.py clear           # delete all stored images (0x14)
  (upload/test replace the stored list via 0x09; pass --append to add to the slideshow instead)
  uv run --with pyusb --with pillow mystique_image.py raw 08 0000     # send any command on EP 0x01
"""
import hashlib
import io
import sys
import time

import usb.core
import usb.util
from PIL import Image, ImageDraw, ImageFont

VID, PID = 0x3633, 0x0009
WIDTH, HEIGHT = 480, 640
EP_CTRL_OUT, EP_CTRL_IN = 0x01, 0x81


def open_device():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("MYSTIQUE not found (is the Windows VM still holding it?)")
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except (NotImplementedError, usb.core.USBError):
        pass
    usb.util.claim_interface(dev, 0)
    return dev


def sum16(data):
    return sum(data) & 0xFFFF


def command(dev, cmd, payload=b""):
    """Send a 48-byte AA 2E control packet on EP 0x01 and return the 55 2E reply."""
    pkt = bytearray(48)
    pkt[0:3] = bytes([0xAA, 0x2E, cmd])
    pkt[3:3 + len(payload)] = payload
    pkt[42:46] = b"HIDC"
    pkt[46:48] = sum16(pkt[:46]).to_bytes(2, "little")
    dev.write(EP_CTRL_OUT, pkt, timeout=2000)
    return bytes(dev.read(EP_CTRL_IN, 64, timeout=2000))


def dcld_header(jpeg, kind=1, frames=0, duration_ms=0, index=0, ident=None):
    hdr = bytearray(64)
    hdr[0:4] = b"DCLd"
    hdr[4] = kind  # 1 = still image, 2 = GIF frame
    hdr[5:9] = len(jpeg).to_bytes(4, "little")
    hdr[9:11] = sum16(jpeg).to_bytes(2, "little")
    hdr[13] = frames
    hdr[15:17] = duration_ms.to_bytes(2, "little")
    hdr[17] = index
    hdr[20:52] = (ident or hashlib.md5(jpeg).hexdigest()).encode()
    hdr[62:64] = sum16(hdr[:62]).to_bytes(2, "little")
    return bytes(hdr)


def to_jpeg(img):
    img = img.convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=85, subsampling=2)  # baseline 4:2:0, like DeepCreative
    return buf.getvalue()


def upload_image(dev, jpeg, slot=0, replace=True):
    t0 = time.monotonic()
    if replace:
        print("0x09 (clear list) reply:", command(dev, 0x09).hex()[:12])
    print("0x0F reply:", command(dev, 0x0F).hex()[:12])
    dev.write(EP_CTRL_OUT, dcld_header(jpeg), timeout=5000)
    for i in range(0, len(jpeg), 64):
        dev.write(EP_CTRL_OUT, jpeg[i:i + 64], timeout=5000)
    dev.write(EP_CTRL_OUT, b"dcldfinish".ljust(64, b"\0"), timeout=5000)
    t1 = time.monotonic()
    print("0x08 reply:", command(dev, 0x08, bytes([0x00, slot])).hex()[:12])
    print("0x03 (image mode) reply:", command(dev, 0x03, b"\x02").hex()[:12])
    print(f"uploaded {len(jpeg)} bytes in {t1 - t0:.3f}s (total {time.monotonic() - t0:.3f}s)")


def test_card():
    img = Image.new("RGB", (WIDTH, HEIGHT), (12, 14, 22))
    d = ImageDraw.Draw(img)
    try:
        big = ImageFont.truetype("DejaVuSans-Bold.ttf", 64)
        small = ImageFont.truetype("DejaVuSans.ttf", 32)
    except OSError:
        big = small = ImageFont.load_default()
    d.rectangle([0, 0, WIDTH - 1, HEIGHT - 1], outline=(0, 200, 255), width=6)
    d.text((WIDTH // 2, 120), "LINUX", font=big, fill=(0, 200, 255), anchor="mm")
    d.text((WIDTH // 2, 200), "custom image test", font=small, fill=(220, 220, 220), anchor="mm")
    d.text((WIDTH // 2, 260), time.strftime("%H:%M:%S"), font=big, fill=(255, 255, 255), anchor="mm")
    for i, (label, color) in enumerate([("TOP", (255, 80, 80)), ("BOTTOM", (80, 255, 80))]):
        d.text((WIDTH // 2, 40 if i == 0 else HEIGHT - 40), label, font=small, fill=color, anchor="mm")
    d.text((20, HEIGHT // 2 + 60), "L", font=big, fill=(255, 200, 0), anchor="lm")
    d.text((WIDTH - 20, HEIGHT // 2 + 60), "R", font=big, fill=(255, 200, 0), anchor="rm")
    return img


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    action = sys.argv[1]
    slot = int(sys.argv[sys.argv.index("--slot") + 1]) if "--slot" in sys.argv else 0
    dev = open_device()
    try:
        if action == "upload":
            upload_image(dev, to_jpeg(Image.open(sys.argv[2])), slot, "--append" not in sys.argv)
        elif action == "test":
            upload_image(dev, to_jpeg(test_card()), slot, "--append" not in sys.argv)
        elif action == "clear":
            print("0x14 reply:", command(dev, 0x14).hex()[:12])
        elif action == "info":
            print("0x03 reply:", command(dev, 0x03, b"\x01").hex()[:12])
        elif action == "raw":
            payload = bytes.fromhex(sys.argv[3]) if len(sys.argv) > 3 else b""
            print("reply:", command(dev, int(sys.argv[2], 16), payload).hex())
        else:
            sys.exit(__doc__)
    finally:
        usb.util.release_interface(dev, 0)


if __name__ == "__main__":
    main()
