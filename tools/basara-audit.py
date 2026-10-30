#!/usr/bin/env python3
"""List active hooks (outside #if 0) still at an NSMBW address (the nsmbw column of hooks.csv
differing from basara), plus the Basara SDK name at that address.
Usage: python -I tools/basara-audit.py"""
import csv, re
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
RT = ROOT / 'runtime' / 'src'
HOOK = re.compile(r"\b(PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
                  r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL|GX_FATAL_STUB)\s*\(\s*(?:0x)?([0-9A-Fa-f]{8})")
names = {}
for line in (ROOT / 'generated/b/basara_sdk_names.tsv').read_text().splitlines():
    n, a = line.split('\t')
    for x in a.split(','):
        names.setdefault(int(x, 16), n)
nsmbw = {}
for r in csv.DictReader(open(ROOT / 'generated/b/hooks.csv')):
    nsmbw[int(r['nsmbw'], 16)] = (r['name'], r['verdict'], r['basara'])
basara = {int(r[2], 16) for r in nsmbw.values() if r[2]}
for f in sorted(RT.rglob('*.[ch]*')):
    depth = []  # stack: True = disabled
    for i, line in enumerate(f.read_text(errors='replace').splitlines(), 1):
        s = line.strip()
        if s.startswith('#if'):
            depth.append(s.startswith('#if 0') or (depth and depth[-1]))
            continue
        if s.startswith('#else'):
            if depth: depth[-1] = (not depth[-1]) if not (len(depth) > 1 and depth[-2]) else True
            continue
        if s.startswith('#endif'):
            if depth: depth.pop()
            continue
        if depth and depth[-1]: continue
        if s.startswith('//') or s.startswith('#define'): continue
        m = HOOK.search(line)
        if not m: continue
        a = int(m.group(2), 16)
        if a in basara and a not in nsmbw: continue
        info = nsmbw.get(a)
        print(f"{f.relative_to(RT)}:{i} {a:08X} nsmbw={info[0] if info else '-'}/{info[1] if info else '-'} basara-name={names.get(a,'?')}")
