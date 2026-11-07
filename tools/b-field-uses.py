#!/usr/bin/env python3
"""Fields read/written through a getter's result in the Basara main.dol.

python -I tools/b-field-uses.py GETTER [WINDOW]

For each `bl GETTER`, follows r3 (and registers copied from it with mr/addi) for WINDOW instructions
(default 40) and prints every load/store through it with its offset, plus addi-derived pointers.
Stops tracking a register when it is overwritten. Rough: no control-flow, so read the listed lines.
"""
import struct
import sys
from pathlib import Path
from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN, CS_MODE_PS

d = Path(__file__).resolve().parent.parent.joinpath('assets/basara-heroes/sys/main.dol').read_bytes()
offs = struct.unpack('>18I', d[0:72]); addrs = struct.unpack('>18I', d[72:144]); sizes = struct.unpack('>18I', d[144:216])
target = int(sys.argv[1], 16)
window = int(sys.argv[2]) if len(sys.argv) > 2 else 40
cs = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN | CS_MODE_PS)


def word(a):
    for i in range(7):
        if addrs[i] <= a < addrs[i] + sizes[i]:
            return struct.unpack('>I', d[offs[i] + a - addrs[i]:offs[i] + a - addrs[i] + 4])[0]
    return None


LOADS = {32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 40: 'lhz', 42: 'lha', 48: 'lfs', 50: 'lfd'}
STORES = {36: 'stw', 37: 'stwu', 38: 'stb', 39: 'stbu', 44: 'sth', 52: 'stfs', 54: 'stfd'}

for i in range(7):
    for k in range(0, sizes[i], 4):
        w = struct.unpack('>I', d[offs[i] + k:offs[i] + k + 4])[0]
        if w >> 26 != 18 or w & 3 != 1:
            continue
        li = w & 0x3FFFFFC
        if li & 0x2000000:
            li -= 0x4000000
        site = addrs[i] + k
        if (site + li) & 0xFFFFFFFF != target:
            continue
        track = {3: 0}  # reg -> offset from the getter's result
        out = []
        a = site + 4
        for _ in range(window):
            w = word(a)
            if w is None:
                break
            ins = next(cs.disasm(w.to_bytes(4, 'big'), a), None)
            txt = '%s %s' % (ins.mnemonic, ins.op_str) if ins else '.long'
            op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            s = imm - 0x10000 if imm & 0x8000 else imm
            hit = None
            if op in LOADS or op in STORES:
                if ra in track:
                    hit = '+0x%X' % (track[ra] + s)
                if op in LOADS and rd in track:
                    del track[rd]
            elif op == 31 and (w >> 1) & 0x3FF in (23, 87, 279, 151, 215, 407):  # lwzx lbzx lhzx stwx stbx sthx
                rb = (w >> 11) & 31
                if ra in track or rb in track:
                    hit = 'indexed +0x%X' % track.get(ra, track.get(rb, 0))
            elif op == 14 and ra in track:  # addi
                track[rd] = track[ra] + s
                hit = 'ptr +0x%X' % track[rd]
            elif op == 31 and (w >> 1) & 0x3FF == 444 and rd == (w >> 11) & 31 and rd in track:  # mr ra, rs
                track[ra] = track[rd]
            elif op == 18 and w & 1:
                track = {k2: v for k2, v in track.items() if k2 >= 14}
            else:
                # Anything else writing rd (most arithmetic forms) drops it.
                if op in (14, 15, 7, 24, 25, 28, 29, 21, 20) and rd in track and op != 14:
                    del track[rd if op not in (24, 25, 28, 29, 21, 20) else ra]
            if hit:
                out.append('    %08X  %-32s %s' % (a, txt, hit))
            if ins and ins.mnemonic == 'blr' or not track:
                break
            a += 4
        print('%08X' % site)
        print('\n'.join(out) if out else '    (no direct use)')
