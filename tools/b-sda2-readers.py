#!/usr/bin/env python3
"""List every load/store of SDA2 (r2) addresses in the Basara main.dol: python -I tools/b-sda2-readers.py ADDR..."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import w1
d = Path(__file__).resolve().parent.parent.joinpath('assets/basara-heroes/sys/main.dol').read_bytes()
secs, _, _, _ = w1.parse_dol(d)
R2 = 0x8066B100
want = {int(a, 16) for a in sys.argv[1:]}
for sec in [s for s in secs if s[3]]:
    off, addr, size = sec[0], sec[1], sec[2]
    for a in range(addr, addr + size, 4):
        w = w1.word(d, secs, a)
        if w is None: continue
        op, ra, imm = w >> 26, (w >> 16) & 31, w & 0xFFFF
        if ra != 2 or op < 32 or op > 55: continue
        t = (R2 + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
        if t in want: print('%08X op%d -> %08X' % (a, op, t))
