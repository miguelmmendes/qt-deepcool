#!/usr/bin/env python3
"""Read-only browsing of the Windows VM disk (~/.windows/data.img) without mounting.

  uv run --with dissect.ntfs winimg.py find <substring> [start_dir]   # case-insensitive name search
  uv run --with dissect.ntfs winimg.py ls <dir>
  uv run --with dissect.ntfs winimg.py get <path> <local_out>
"""
import sys
from dissect.ntfs import NTFS
from dissect.util.stream import RangeStream

IMAGE = "/home/bulletazz/.windows/data.img"
OFFSET, SIZE = 269484032, 130074 * 2**20      # GPT partition 2 ("Basic data partition")

fs = NTFS(RangeStream(open(IMAGE, "rb"), OFFSET, SIZE))


def node(path):
    return fs.mft.get(path.replace("/", "\\"))


def walk(rec, path, needle, depth=0, limit=8):
    try:
        entries = rec.listdir()
    except Exception:
        return
    for name, entry in entries.items():
        if name in (".", ".."):
            continue
        try:
            child = entry.dereference()
        except Exception:
            continue
        full = f"{path}/{name}"
        if needle in name.lower():
            print(full)
        if depth < limit and child.is_dir():
            walk(child, full, needle, depth + 1, limit)


cmd = sys.argv[1]
if cmd == "find":
    start = sys.argv[3] if len(sys.argv) > 3 else ""
    walk(node(start) if start else fs.mft.get("\\"), start, sys.argv[2].lower())
elif cmd == "ls":
    for name, entry in sorted(node(sys.argv[2]).listdir().items()):
        if name in (".", ".."):
            continue
        child = entry.dereference()
        size = child.size() if not child.is_dir() else "<dir>"
        print(f"{size!s:>12}  {name}")
elif cmd == "get":
    with open(sys.argv[3], "wb") as out:
        out.write(node(sys.argv[2]).open().read())
