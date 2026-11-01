"""CRI AFS archives of the Basara disc: list entries and count their first-4-byte magics.

Usage: python -I tools/b-afs.py ARCHIVE.afs [--magic] [--dump OUTDIR [--first N]]
"""
import collections
import os
import struct
import sys


def entries(f):
    f.seek(0)
    head = f.read(8)
    if head[:4] != b"AFS\0":
        raise SystemExit("not an AFS archive")
    count = struct.unpack_from("<I", head, 4)[0]
    table = f.read(8 * count)
    return [struct.unpack_from("<II", table, 8 * i) for i in range(count)]


def main():
    path = sys.argv[1]
    args = sys.argv[2:]
    with open(path, "rb") as f:
        ents = entries(f)
        print(f"{len(ents)} entries")
        if "--magic" in args:
            magics = collections.Counter()
            sample = {}
            for i, (off, size) in enumerate(ents):
                if size == 0:
                    continue
                f.seek(off)
                m = f.read(16)
                key = m[:4]
                magics[key] += 1
                sample.setdefault(key, (i, size, m.hex()))
            for key, n in magics.most_common(40):
                i, size, hx = sample[key]
                print(f"{n:6} {key!r:24} e.g. #{i} size {size} {hx}")
        if "--dump" in args:
            out = args[args.index("--dump") + 1]
            first = int(args[args.index("--first") + 1]) if "--first" in args else len(ents)
            os.makedirs(out, exist_ok=True)
            for i, (off, size) in enumerate(ents[:first]):
                f.seek(off)
                with open(os.path.join(out, f"{i:05}.bin"), "wb") as o:
                    o.write(f.read(size))


main()
