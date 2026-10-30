#!/usr/bin/env python3
"""Move NSMBW runtime hooks to Basara addresses through SDK names.

1. Match main.dol functions against Dolphin totaldb.dsy (checksum + size) -> SDK name -> Basara address.
2. Find every addressed hook in runtime/src; NSMBW address -> name through local/upstream/nsmbw/MAP.txt.
3. --apply: rewrite the addresses found; wrap hooks missing from Basara in "#if 0".

Pakai: python -I tools/b_remap.py [--apply]
"""
import argparse
import csv
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import w1  # noqa: E402

DSY = ROOT / 'refs' / 'NWiiRecomp' / 'third_party' / 'dolphin-sigdb' / 'totaldb.dsy'
DOL = ROOT / 'assets' / 'basara-heroes' / 'sys' / 'main.dol'
SEED = ROOT / 'generated' / 'w1' / 'basara-heroes-seed' / 'seed_map.txt'
MAP = ROOT / 'local' / 'upstream' / 'nsmbw' / 'MAP.txt'
RT = ROOT / 'runtime' / 'src'
OUT = ROOT / 'generated' / 'b'

HOOK = re.compile(
    r"\b(PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?|REGISTER_TRANSLATED_FUNCTION|"
    r"MKW_KNOWN_TYPED_NATIVE|MKW_KNOWN_NATIVE_CPU_CALL|GX_FATAL_STUB)\s*\(\s*((?:0x)?)([0-9A-Fa-f]{8})(u?)")


def checksum_step(s, opcode):
    """Port Dolphin HashSignatureDB::ComputeCodeChecksum (satu instruksi)."""
    op = opcode & 0xFC000000
    op2 = op3 = 0
    aux = op >> 26
    if aux == 4:
        op2 = opcode & 0x3F
        if op2 in (0, 8, 16, 21, 22):
            op3 = opcode & 0x7C0
    elif 7 <= aux <= 15:
        op2 = opcode & 0x03FF0000
    elif aux in (19, 31, 63):
        op2 = opcode & 0x7FF
    elif aux == 59:
        op2 = opcode & 0x3F
        if op2 < 16:
            op3 = opcode & 0x7C0
    elif 32 <= aux < 56:
        op2 = opcode & 0x03FF0000
    s = ((s << 17) & 0xFFFE0000) | ((s >> 15) & 0x0001FFFF)
    return s ^ (op | op2 | op3)


def load_dsy():
    b = DSY.read_bytes()
    n = struct.unpack_from('<I', b, 0)[0]
    db = defaultdict(set)
    for i in range(n):
        ck, size, name = struct.unpack_from('<II128s', b, 4 + 136 * i)
        db[(ck, size)].add(name.split(b'\0')[0].decode('ascii', 'replace').split('\t')[0].strip())
    return db


def match_dol(db):
    data = w1.Path(DOL).read_bytes()
    secs, _, _, _ = w1.parse_dol(data)
    text = [(o, a, s) for o, a, s, t in secs if t]
    starts = sorted(int(l.split()[0], 16) for l in SEED.read_text().splitlines() if l.strip())
    names = defaultdict(set)
    for idx, start in enumerate(starts):
        seg = next(((o, a, s) for o, a, s in text if a <= start < a + s), None)
        if seg is None:
            continue
        o, a, s = seg
        limit = min(starts[idx + 1] if idx + 1 < len(starts) else a + s, a + s, start + 0x8000)
        p = o + start - a
        sm = 0
        for k in range((limit - start) // 4):
            sm = checksum_step(sm, struct.unpack_from('>I', data, p + 4 * k)[0])
            hit = db.get((sm, 4 * (k + 1)))
            if hit:
                for nm in hit:
                    names[nm].add(start)
    return names, len(starts)


def load_map():
    by_addr = {}
    for line in MAP.read_text(errors='replace').splitlines():
        p = line.split(None, 1)
        if len(p) == 2 and not p[1].startswith('0x'):
            by_addr[int(p[0], 16)] = p[1].strip()
    return by_addr


def inventory():
    rows = []
    for f in sorted(RT.rglob('*')):
        if f.suffix not in ('.cpp', '.h', '.hpp', '.inc'):
            continue
        for i, line in enumerate(f.read_text(encoding='utf-8', errors='surrogateescape').splitlines(), 1):
            for m in HOOK.finditer(line):
                rows.append((f, i, m.group(1), int(m.group(3), 16)))
    return rows


def statement_end(lines, start, col):
    depth, started = 0, False
    for i in range(start, len(lines)):
        for ch in (lines[i][col:] if i == start else lines[i]):
            if ch == '(':
                depth += 1
                started = True
            elif ch == ')':
                depth -= 1
                if started and depth == 0:
                    return i
    return start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    names, nfunc = match_dol(load_dsy())
    print('Basara: %d functions tested, %d SDK names matched' % (nfunc, len(names)))
    (OUT / 'basara_sdk_names.tsv').write_text(
        ''.join('%s\t%s\n' % (n, ','.join('%08X' % x for x in sorted(v))) for n, v in sorted(names.items())))
    mp = load_map()
    inv = inventory()
    verdicts = {}
    stats = defaultdict(int)
    with (OUT / 'hooks.csv').open('w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['file', 'line', 'macro', 'nsmbw', 'name', 'verdict', 'basara'])
        for f, ln, macro, addr in inv:
            nm = mp.get(addr, '')
            cand = sorted(names.get(nm, ())) if nm else []
            v = 'resolved' if len(cand) == 1 else 'multi' if cand else ('noname' if not nm else 'absent')
            if v == 'multi':
                cand = cand[:1]
            verdicts[(f, ln, addr)] = (v, cand[0] if cand else None)
            stats[v] += 1
            w.writerow([f.relative_to(RT).as_posix(), ln, macro, '%08X' % addr, nm, v,
                        '%08X' % cand[0] if cand else ''])
    print('hook:', len(inv), dict(stats), '-> %s' % (OUT / 'hooks.csv'))
    if not a.apply:
        return

    by_file = defaultdict(list)
    for (f, ln, addr), (v, new) in verdicts.items():
        by_file[f].append((ln, addr, v, new))
    changed = defaultdict(int)
    for f, items in by_file.items():
        lines = f.read_text(encoding='utf-8', errors='surrogateescape').splitlines(keepends=True)
        disable = []
        for ln, addr, v, new in items:
            i = ln - 1
            for m in HOOK.finditer(lines[i]):
                if int(m.group(3), 16) != addr:
                    continue
                if v in ('resolved', 'multi'):
                    tok = '%08X' % new
                    if m.group(3) == m.group(3).lower() and any(c.isalpha() for c in m.group(3)):
                        tok = tok.lower()
                    lines[i] = lines[i][:m.start(3)] + tok + lines[i][m.end(3):]
                    changed['dialamat'] += 1
                else:
                    disable.append((i, statement_end(lines, i, m.start())))
                break
        for s, e in sorted(set(disable), reverse=True):
            ind = re.match(r'\s*', lines[s]).group(0)
            lines.insert(e + 1, ind + '#endif\n')
            lines.insert(s, ind + '#if 0\n')
            changed['dimatikan'] += 1
        f.write_text(''.join(lines), encoding='utf-8', errors='surrogateescape')
    print('apply:', dict(changed))


if __name__ == '__main__':
    main()
