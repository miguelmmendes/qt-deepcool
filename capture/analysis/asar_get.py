#!/usr/bin/env python3
"""Extract files from an Electron app.asar:  asar_get.py app.asar <out_dir> <path-substring>..."""
import json, os, struct, sys

asar, out_dir, needles = sys.argv[1], sys.argv[2], sys.argv[3:]
f = open(asar, "rb")
header_size = struct.unpack("<I", f.read(16)[12:16])[0]
f.seek(0)
data_offset = 8 + struct.unpack("<I", f.read(8)[4:8])[0]
f.seek(16)
header = json.loads(f.read(header_size))


def walk(node, path):
    for name, n in node.get("files", {}).items():
        p = f"{path}/{name}"
        if "files" in n:
            yield from walk(n, p)
        elif "offset" in n:
            yield p, int(n["offset"]), int(n["size"])


for path, offset, size in walk(header, ""):
    if any(n in path for n in needles):
        f.seek(data_offset + offset)
        dest = os.path.join(out_dir, path.lstrip("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        open(dest, "wb").write(f.read(size))
        print(f"{size:>9}  {path}")
