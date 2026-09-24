#!/usr/bin/env python3
"""Extract DeepCool Mystique control commands and image uploads from usbmon captures."""
import subprocess, sys, hashlib, os

def find_cooler(path):
    """USB address whose bulk packets carry the AA 2E / 55 2E protocol header."""
    out = subprocess.run(['tshark', '-r', path, '-Y', 'usb.transfer_type==0x03 && usb.data_len>0',
        '-T', 'fields', '-e', 'usb.device_address', '-e', 'usb.capdata'],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        addr, _, data = line.partition('\t')
        if data.startswith(('aa2e', '552e')):
            return int(addr)
    return None


def packets(path, dev=None):
    dev = dev if dev is not None else find_cooler(path)
    if dev is None:
        return
    out = subprocess.run(['tshark', '-r', path, '-Y',
        f'usb.device_address=={dev} && usb.transfer_type==0x03 && usb.data_len>0',
        '-T', 'fields', '-e', 'frame.time_relative', '-e', 'usb.endpoint_address', '-e', 'usb.capdata'],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        t, ep, data = line.split('\t')
        yield float(t), int(ep, 16), bytes.fromhex(data)

def main(path, outdir):
    name = os.path.splitext(os.path.basename(path))[0]
    upload = None; n = 0
    for t, ep, d in packets(path):
        if upload is not None and ep == 0x01:
            if d.startswith(b'dcldfinish'):
                body = bytes(upload['data'][:upload['len']])
                ext = 'jpg' if body[:2] == b'\xff\xd8' else 'gif' if body[:3] == b'GIF' else 'bin'
                fn = f'{outdir}/{name}_{n}.{ext}'; open(fn, 'wb').write(body); n += 1
                md5 = hashlib.md5(body).hexdigest()
                print(f'{t:8.3f}   ...{len(upload["data"])} bytes streamed, finish -> {fn}  md5 ok={md5 == upload["md5"]}')
                upload = None
            else:
                upload['data'] += d
            continue
        if d[:4] == b'DCLd' and ep == 0x01:
            ln = int.from_bytes(d[5:9], 'little')
            upload = {'len': ln, 'md5': d[20:52].decode(errors='replace'), 'data': bytearray()}
            print(f'{t:8.3f} EP{ep:02x} DCLd hdr: type={d[4]:#x} len={ln} field9={d[9:13].hex()} md5={upload["md5"]} tail={d[52:].hex()}')
            continue
        if d[:2] in (b'\xaa\x2e', b'\x55\x2e'):
            cmd = d[2]
            if ep in (0x02, 0x82) and cmd in (0x10, 0x01) and '-v' not in sys.argv:
                continue  # periodic status/data polling
            dirn = 'OUT' if ep < 0x80 else 'IN '
            payload = d[3:42].rstrip(b'\0')
            print(f'{t:8.3f} EP{ep:02x} {dirn} cmd={cmd:#04x} payload={payload.hex()}')
        else:
            print(f'{t:8.3f} EP{ep:02x} RAW {d[:32].hex()}')

if __name__ == '__main__':
    outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'extracted'); os.makedirs(outdir, exist_ok=True)
    for p in [a for a in sys.argv[1:] if not a.startswith('-')]:
        print(f'===== {p}'); main(p, outdir)
