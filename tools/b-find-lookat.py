#!/usr/bin/env python3
"""Find SDK C_MTXLookAt candidates in the Basara main.dol: functions whose first calls are
A, B, A, B (VECNormalize, VECCrossProduct twice) after a few fsubs. Also lists every caller.
python -I tools/b-find-lookat.py"""
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
TEXT0, TEXT1 = 0x80028EE0, 0x80028EE0 + 4629248


def bl_target(a, w):
    if (w >> 26) == 18 and (w & 3) == 1:
        off = w & 0x03FFFFFC
        if off & 0x02000000:
            off -= 0x04000000
        return (a + off) & 0xFFFFFFFF
    return None


funcs = [a for a in addrs if TEXT0 <= a < TEXT1]
cands = []
for i, f in enumerate(funcs):
    end = funcs[i + 1] if i + 1 < len(funcs) else TEXT1
    if end - f > 0x400:
        continue
    calls, fsubs = [], 0
    for a in range(f, end, 4):
        w = w1.word(d, secs, a)
        if w is None:
            break
        if (w >> 26) == 59 and ((w >> 1) & 31) == 20:  # fsubs
            fsubs += 1
        t = bl_target(a, w)
        if t is not None:
            calls.append(t)
    if len(calls) >= 4 and fsubs >= 3:
        a_, b_, c_, d_ = calls[:4]
        if a_ == c_ and b_ == d_ and a_ != b_:
            cands.append((f, end, calls[:4], fsubs))

for f, end, calls, fsubs in cands:
    print('candidate %08X-%08X calls %s fsubs %d' % (f, end, ' '.join('%08X' % c for c in calls), fsubs))
    callers = []
    for a in range(TEXT0, TEXT1, 4):
        w = w1.word(d, secs, a)
        if w is not None and bl_target(a, w) == f:
            callers.append(a)
    print('  %d callers: %s' % (len(callers), ' '.join('%08X' % c for c in callers[:40])))
