"""Extracts every TPL texture from the Basara disc's linkData.afs as PNG, named the way the runtime's
texture replacement looks them up (tex1_WxH_<xxh64 data>[_<xxh64 palette>]_FMT.png).

Edit a PNG (any larger size with the same aspect works) and copy it into the textures folder; the
runtime indexes that folder recursively. Only copy what you changed: an unedited copy costs memory.

Compressed entries are read from UNPACKED (tools/b-unpack.c output) when present there.

Usage: python -I tools/b-tpl-extract.py [AFS] [OUTDIR] [UNPACKED]
  defaults: assets/basara-heroes/files/linkData.afs  generated/textures-iso  generated/afs-unpacked
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

M = (1 << 64) - 1
P1, P2, P3, P4, P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                      0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M


def _round(acc, v):
    return (_rotl((acc + v * P2) & M, 31) * P1) & M


def _merge(acc, v):
    return ((acc ^ _round(0, v)) * P1 + P4) & M


def xxh64(data, seed=0):
    n = len(data)
    p = 0
    if n >= 32:
        v1, v2, v3, v4 = (seed + P1 + P2) & M, (seed + P2) & M, seed, (seed - P1) & M
        words = struct.unpack_from(f"<{(n // 32) * 4}Q", data)
        for i in range(0, len(words), 4):
            v1 = _round(v1, words[i])
            v2 = _round(v2, words[i + 1])
            v3 = _round(v3, words[i + 2])
            v4 = _round(v4, words[i + 3])
        h = (_rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18)) & M
        for v in (v1, v2, v3, v4):
            h = _merge(h, v)
        p = (n // 32) * 32
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while p + 8 <= n:
        h ^= _round(0, struct.unpack_from("<Q", data, p)[0])
        h = (_rotl(h, 27) * P1 + P4) & M
        p += 8
    if p + 4 <= n:
        h ^= (struct.unpack_from("<I", data, p)[0] * P1) & M
        h = (_rotl(h, 23) * P2 + P3) & M
        p += 4
    while p < n:
        h ^= (data[p] * P5) & M
        h = (_rotl(h, 11) * P1) & M
        p += 1
    h ^= h >> 33
    h = (h * P2) & M
    h ^= h >> 29
    h = (h * P3) & M
    h ^= h >> 32
    return h


# GX format -> (block width, block height, bits per pixel)
BLOCK = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
         6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 10: (4, 4, 16), 14: (8, 8, 4)}
PALETTED = {8, 9, 10}


def buffer_size(fmt, w, h):
    bw, bh, bpp = BLOCK[fmt]
    return ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bw * bh * bpp // 8


def _untile(px, w, h, bw, bh):
    nbx, nby = (w + bw - 1) // bw, (h + bh - 1) // bh
    a = px.reshape(nby, nbx, bh, bw, 4).transpose(0, 2, 1, 3, 4).reshape(nby * bh, nbx * bw, 4)
    return a[:h, :w]


def _x5(v):
    return (v << 3) | (v >> 2)


def _rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([_x5(r), (g << 2) | (g >> 4), _x5(b), np.full_like(v, 255)], -1)


def _rgb5a3(v):
    opaque = (v & 0x8000) != 0
    r5, g5, b5 = (v >> 10) & 31, (v >> 5) & 31, v & 31
    r4, g4, b4, a3 = (v >> 8) & 15, (v >> 4) & 15, v & 15, (v >> 12) & 7
    r = np.where(opaque, _x5(r5), r4 * 17)
    g = np.where(opaque, _x5(g5), g4 * 17)
    b = np.where(opaque, _x5(b5), b4 * 17)
    a = np.where(opaque, 255, (a3 << 5) | (a3 << 2) | (a3 >> 1))
    return np.stack([r, g, b, a], -1)


def _palette(fmt, raw, count):
    v = np.frombuffer(raw[:count * 2], ">u2").astype(np.int32)
    if fmt == 0:  # IA8
        i, a = v & 255, v >> 8
        return np.stack([i, i, i, a], -1)
    return _rgb565(v) if fmt == 1 else _rgb5a3(v)


def decode(fmt, w, h, data, pal=None):
    bw, bh, _ = BLOCK[fmt]
    b = np.frombuffer(data[:buffer_size(fmt, w, h)], np.uint8).astype(np.int32)
    if fmt in (0, 8):
        b = np.stack([b >> 4, b & 15], -1).reshape(-1)
    if fmt == 0:
        px = np.stack([b * 17] * 3 + [b * 17], -1)
    elif fmt == 1:
        px = np.stack([b] * 4, -1)
    elif fmt == 2:
        i, a = (b & 15) * 17, (b >> 4) * 17
        px = np.stack([i, i, i, a], -1)
    elif fmt == 3:
        a, i = b[0::2], b[1::2]
        px = np.stack([i, i, i, a], -1)
    elif fmt in (4, 5, 10):
        v = (b[0::2] << 8) | b[1::2]
        px = _rgb565(v) if fmt == 4 else _rgb5a3(v) if fmt == 5 else pal[np.minimum(v & 0x3FFF, len(pal) - 1)]
    elif fmt == 6:
        blk = b.reshape(-1, 2, 16, 2)
        px = np.stack([blk[:, 0, :, 1], blk[:, 1, :, 0], blk[:, 1, :, 1], blk[:, 0, :, 0]], -1).reshape(-1, 4)
    elif fmt in (8, 9):
        px = pal[np.minimum(b, len(pal) - 1)]
    elif fmt == 14:
        return _cmpr(b, w, h)
    return _untile(px, w, h, bw, bh).astype(np.uint8)


def _cmpr(b, w, h):
    nbx, nby = (w + 7) // 8, (h + 7) // 8
    s = b.reshape(-1, 8)
    c0 = (s[:, 0] << 8) | s[:, 1]
    c1 = (s[:, 2] << 8) | s[:, 3]
    e0, e1 = _rgb565(c0), _rgb565(c1)
    four = (c0 > c1)[:, None]
    c2 = np.where(four, (2 * e0 + e1) // 3, (e0 + e1) // 2)
    c3 = np.where(four, (e0 + 2 * e1) // 3, 0)
    pal = np.stack([e0, e1, c2, c3], 1)
    bits = s[:, 4:8]
    idx = np.stack([(bits >> sh) & 3 for sh in (6, 4, 2, 0)], -1).reshape(-1, 16)
    px = np.take_along_axis(pal, idx[:, :, None], 1)
    px = px.reshape(nby, nbx, 2, 2, 4, 4, 4).transpose(0, 2, 4, 1, 3, 5, 6).reshape(nby * 8, nbx * 8, 4)
    return px[:h, :w].astype(np.uint8)


def referenced_palette_hash(fmt, w, h, data, palraw):
    b = np.frombuffer(data[:buffer_size(fmt, w, h)], np.uint8)
    if fmt == 8:
        idx = np.concatenate([b >> 4, b & 15])
    elif fmt == 9:
        idx = b
    else:
        idx = np.frombuffer(b.tobytes(), ">u2") & 0x3FFF
    lo, hi = int(idx.min()), int(idx.max())
    sl = palraw[2 * lo:2 * (hi + 1)]
    return xxh64(sl) if len(sl) == 2 * (hi + 1 - lo) else 0


def parse_tpl(buf, base):
    """Yields (fmt, w, h, data, palfmt, palraw, palcount) of each image of the TPL at `base`."""
    count, table = struct.unpack_from(">II", buf, base + 4)
    if not (0 < count < 1024) or base + table + 8 * count > len(buf):
        return
    for i in range(count):
        ih, ph = struct.unpack_from(">II", buf, base + table + 8 * i)
        if base + ih + 12 > len(buf):
            return
        h, w, fmt, doff = struct.unpack_from(">HHII", buf, base + ih)
        if fmt not in BLOCK or not w or not h or w > 2048 or h > 2048:
            continue
        size = buffer_size(fmt, w, h)
        if base + doff + size > len(buf):
            continue
        data = buf[base + doff:base + doff + size]
        palfmt = palraw = palcount = None
        if fmt in PALETTED:
            if not ph:
                continue
            palcount, _, _, palfmt, poff = struct.unpack_from(">HBBII", buf, base + ph)
            palraw = buf[base + poff:base + poff + 2 * palcount]
        yield fmt, w, h, data, palfmt, palraw, palcount


def afs_entries(f):
    f.seek(4)
    n = struct.unpack("<I", f.read(4))[0]
    table = [struct.unpack("<II", f.read(8)) for _ in range(n)]
    f.seek(8 + 8 * n)
    noff, _ = struct.unpack("<II", f.read(8))
    names = []
    if noff:
        f.seek(noff)
        raw = f.read(48 * n)
        names = [raw[48 * i:48 * i + 32].split(b"\0")[0].decode("ascii", "replace") for i in range(n)]
    return [(off, size, names[i] if i < len(names) and names[i] else f"{i:05}") for i, (off, size) in
            enumerate(table)]


MAGIC = b"\x00\x20\xaf\x30"


def main():
    afs = sys.argv[1] if len(sys.argv) > 1 else "assets/basara-heroes/files/linkData.afs"
    out = sys.argv[2] if len(sys.argv) > 2 else "generated/textures-iso"
    seen = set()
    written = 0
    unpacked = sys.argv[3] if len(sys.argv) > 3 else "generated/afs-unpacked"
    with open(afs, "rb") as f:
        for i, (off, size, name) in enumerate(afs_entries(f)):
            if not size:
                continue
            f.seek(off)
            buf = f.read(size)
            packed = os.path.join(unpacked, f"{i:05}.bin")
            if os.path.isfile(packed):
                with open(packed, "rb") as u:
                    buf = u.read()
            pos = buf.find(MAGIC)
            while pos >= 0:
                for fmt, w, h, data, palfmt, palraw, palcount in parse_tpl(buf, pos):
                    th = xxh64(data)
                    if fmt in PALETTED:
                        fname = (f"tex1_{w}x{h}_{th:016x}_"
                                 f"{referenced_palette_hash(fmt, w, h, data, palraw):016x}_{fmt}.png")
                    else:
                        fname = f"tex1_{w}x{h}_{th:016x}_{fmt}.png"
                    if fname in seen:
                        continue
                    seen.add(fname)
                    pal = _palette(palfmt, palraw, palcount) if fmt in PALETTED else None
                    folder = os.path.join(out, f"{i:05}_{name.replace('/', '_')}")
                    os.makedirs(folder, exist_ok=True)
                    Image.fromarray(decode(fmt, w, h, data, pal), "RGBA").save(os.path.join(folder, fname))
                    written += 1
                pos = buf.find(MAGIC, pos + 4)
            if i % 500 == 0:
                print(f"entry {i}: {written} textures", flush=True)
    print(f"{written} textures in {out}")


if __name__ == "__main__":
    main()
