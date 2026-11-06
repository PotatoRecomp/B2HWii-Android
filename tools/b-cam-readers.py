#!/usr/bin/env python3
"""List code that loads the camera pointer (SDA 806626A0, lwz rX,-0x55E0(r13)) and then reads the
camera's eye (+0xAEC), target (+0xACC) or view matrix (+0xA7C) within the next 40 instructions,
outside the camera module (8004B000..80054000). python -I tools/b-cam-readers.py"""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import w1

root = Path(__file__).resolve().parent.parent
d = root.joinpath('assets/basara-heroes/sys/main.dol').read_bytes()
secs, _, _, _ = w1.parse_dol(d)
table = root.joinpath('generated/guest_symbol_table.cpp').read_text()
addrs = sorted(int(m, 16) for m in re.findall(r'^\s+0x([0-9A-F]{8})u,', table, re.M))
import bisect
T0, T1 = 0x80028EE0, 0x80028EE0 + 4629248
FIELDS = {0xAEC: 'eye', 0xACC: 'target', 0xA7C: 'view', 0xAFC: 'eye_prev', 0xADC: 'target_prev'}
hits = {}
for a in range(T0, T1, 4):
    if 0x8004B000 <= a < 0x80054000:
        continue
    w = w1.word(d, secs, a)
    if w is None:
        continue
    op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
    if op == 32 and ra == 13 and imm == (0x10000 - 0x55E0):  # lwz rD, -0x55E0(r13)
        regs = {rd}
        for b in range(a + 4, a + 4 * 40, 4):
            v = w1.word(d, secs, b)
            if v is None:
                break
            o2, d2, a2, i2 = v >> 26, (v >> 21) & 31, (v >> 16) & 31, v & 0xFFFF
            if a2 in regs and i2 in FIELDS and o2 in (14, 48, 50, 56, 32):  # addi/lfs/lfd/psq_l/lwz
                f = addrs[bisect.bisect_right(addrs, a) - 1]
                hits.setdefault(f, set()).add(FIELDS[i2])
            if o2 == 31 and ((v >> 1) & 0x3FF) == 444 and ((v >> 21) & 31) in regs:  # mr
                regs.add((v >> 16) & 31)
for f in sorted(hits):
    print('%08X %s' % (f, ','.join(sorted(hits[f]))))
