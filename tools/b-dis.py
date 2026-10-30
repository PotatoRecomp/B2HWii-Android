#!/usr/bin/env python3
"""Disassemble a function of the Basara main.dol: python -I tools/b-dis.py START_ADDR [END_ADDR]
Prints instructions with SDA (r13/r2) references and lis/addi/ori resolved."""
import sys
from pathlib import Path
from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN, CS_MODE_PS
sys.path.insert(0, str(Path(__file__).resolve().parent))
import w1
d = Path(__file__).resolve().parent.parent.joinpath('assets/basara-heroes/sys/main.dol').read_bytes()
secs, _, _, _ = w1.parse_dol(d)
a0 = int(sys.argv[1], 16)
a1 = int(sys.argv[2], 16) if len(sys.argv) > 2 else a0 + 0x200
# Gekko paired singles (ps_*, psq_*), otherwise decoded as AltiVec.
cs = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN | CS_MODE_PS)
R13, R2 = 0x80667C80, 0x8066B100
hi = {}
for a in range(a0, a1, 4):
    w = w1.word(d, secs, a)
    if w is None: break
    ins = next(cs.disasm(w.to_bytes(4, 'big'), a), None)
    txt = '%s %s' % (ins.mnemonic, ins.op_str) if ins else '.long 0x%08X' % w
    note = ''
    op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
    s = imm - 0x10000 if imm & 0x8000 else imm
    if op == 15 and ra == 0: hi[rd] = imm << 16
    if op in (32, 34, 36, 38, 40, 44, 48, 50, 52, 54) or op in (33, 35, 37, 39, 41, 45):
        if ra == 13: note = '  ; SDA r13 -> %08X' % ((R13 + s) & 0xFFFFFFFF)
        elif ra == 2: note = '  ; SDA2 r2 -> %08X' % ((R2 + s) & 0xFFFFFFFF)
        elif ra in hi: note = '  ; %08X' % ((hi[ra] + s) & 0xFFFFFFFF)
    if op == 14 and ra in hi: note = '  ; %08X' % ((hi[ra] + s) & 0xFFFFFFFF)
    if op == 14 and ra == 13: note = '  ; &SDA %08X' % ((R13 + s) & 0xFFFFFFFF)
    if op == 14 and ra == 2: note = '  ; &SDA2 %08X' % ((R2 + s) & 0xFFFFFFFF)
    print('%08X  %s%s' % (a, txt, note))
