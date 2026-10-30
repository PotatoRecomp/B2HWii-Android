#!/usr/bin/env python3
"""W1 feasibility test: main.dol -> find SDA bases -> manifest -> translate-recursive -> summary.

Usage:
  python tools/w1.py assets/basara/main.dol [--map symbols.txt]
  python tools/w1.py --selftest        # synthetic DOL, checks the path only
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOTNET = ROOT / 'third_party' / 'dotnet' / 'dotnet.exe'
CLI = (ROOT / 'third_party' / 'wc-translator' / 'translator' / 'src' / 'Translator.Cli'
       / 'bin' / 'Release' / 'net8.0' / 'Translator.Cli.dll')
BLR = 0x4E800020


def parse_dol(data):
    be = lambda off, n: struct.unpack('>%dI' % n, data[off:off + 4 * n])
    offs, addrs, sizes = be(0x00, 18), be(0x48, 18), be(0x90, 18)
    bss_addr, bss_size, entry = be(0xD8, 3)
    secs = [(o, a, s, i < 7) for i, (o, a, s) in enumerate(zip(offs, addrs, sizes)) if s]
    return secs, entry, bss_addr, bss_size


def word(data, secs, addr):
    for off, a, s, _ in secs:
        if a <= addr < a + s:
            p = off + addr - a
            return struct.unpack('>I', data[p:p + 4])[0]
    return None


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def scan(data, secs, start, count, stop_at_blr):
    """Read simple lis/ori/addi; return (register values, first bl target)."""
    vals, first_bl = {}, None
    for i in range(count):
        pc = start + 4 * i
        w = word(data, secs, pc)
        if w is None:
            break
        op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 15 and ra == 0:                      # lis rD,imm
            vals[rd] = (imm << 16) & 0xFFFFFFFF
        elif op == 24 and rd in vals:                 # ori rA,rS,imm (rd = RS in the encoding)
            vals[ra] = vals[rd] | imm
        elif op == 14 and ra in vals:                 # addi rD,rA,simm
            vals[rd] = (vals[ra] + sext(imm, 16)) & 0xFFFFFFFF
        elif op == 18 and (w & 3) == 1 and first_bl is None:   # bl
            first_bl = (pc + (sext(w & 0x03FFFFFC, 26))) & 0xFFFFFFFF
        if stop_at_blr and w == BLR:
            break
    return vals, first_bl


def find_sda(data, secs, entry):
    _, init = scan(data, secs, entry, 48, False)
    if init is not None:
        vals, _ = scan(data, secs, init, 64, True)
        if 2 in vals and 13 in vals:
            return vals[13], vals[2], 'from __init_registers @%08X' % init
    vals, _ = scan(data, secs, entry, 256, False)
    if 2 in vals and 13 in vals:
        return vals[13], vals[2], 'fallback: scanned 256 instructions from entry (CHECK BY HAND)'
    return None, None, 'not found'


def write_manifest(path, dol, sda, sda2, fmap):
    lines = [
        'schema_version: 1', 'workspace_root: .', '',
        'project:', '  id: %s' % path.parent.name, '  display_name: W1 %s' % path.parent.name, '',
        'memory:', '  base: 0x80000000', '  size: 0x01800000',
        '  sda_base: 0x%08X' % sda, '  sda2_base: 0x%08X' % sda2, '',
        'inputs:', '  dol:', '    path: "%s"' % dol.resolve().as_posix(), '',
        'translation:', '  allow_unsupported_instructions: true',
    ]
    if fmap:
        lines += ['  function_map:', '    path: "%s"' % Path(fmap).resolve().as_posix()]
    lines += ['', 'runtime:', '  native_abi_directories: []', '',
              'output:', '  root: out', '  functions: functions', '  runtime_config: RuntimeConfig.h',
              '  data_initializer: data_sections_init.cpp', '  base_manifest: base/base_manifest.json', '']
    path.write_text('\n'.join(lines), encoding='utf-8')


def seed_map(data, secs, path):
    """Extra function starts: bl targets in code + data-section pointers aimed at a prologue."""
    text = [(o, a, s) for o, a, s, t in secs if t]
    in_text = lambda x: x % 4 == 0 and any(a <= x < a + s for _, a, s in text)
    is_prolog = lambda x: any(
        (lambda w: w == 0x7C0802A6 or (w >> 26 == 37 and (w >> 21) & 31 == 1 and (w >> 16) & 31 == 1
                                       and w & 0x8000))(word(data, secs, x + 4 * k) or 0) for k in range(2))
    bl_targets = set()
    for o, a, s in text:
        for i in range(0, s, 4):
            w = struct.unpack('>I', data[o + i:o + i + 4])[0]
            if w >> 26 == 18 and (w & 3) == 1:
                t = (a + i + sext(w & 0x03FFFFFC, 26)) & 0xFFFFFFFF
                if in_text(t):
                    bl_targets.add(t)
    ptr_targets = set()
    for o, a, s, t in secs:
        if t:
            continue
        for i in range(0, s - 3, 4):
            v = struct.unpack('>I', data[o + i:o + i + 4])[0]
            if in_text(v) and (v in bl_targets or is_prolog(v)):
                ptr_targets.add(v)
    seeds = sorted(bl_targets | ptr_targets)
    path.write_text(''.join('%08X sub_%08X\n' % (x, x) for x in seeds), encoding='ascii')
    print('seed: %d bl targets + %d new data pointers = %d starts -> %s'
          % (len(bl_targets), len(ptr_targets - bl_targets), len(seeds), path))


def make_selftest_dol(path):
    lis = lambda r, v: (15 << 26) | (r << 21) | (v & 0xFFFF)
    ori = lambda r, v: (24 << 26) | (r << 21) | (r << 16) | (v & 0xFFFF)
    code = [0x48000009, BLR,                       # start: bl init ; blr
            lis(1, 0x8000), ori(1, 0x1000), lis(2, 0x8040), ori(2, 0x2000),
            lis(13, 0x8040), ori(13, 0x1000), BLR]  # init
    addr = 0x80003100
    body = b''.join(struct.pack('>I', w) for w in code)
    hdr = bytearray(0x100)
    struct.pack_into('>I', hdr, 0x00, 0x100)
    struct.pack_into('>I', hdr, 0x48, addr)
    struct.pack_into('>I', hdr, 0x90, len(body))
    struct.pack_into('>I', hdr, 0xE0, addr)
    path.write_bytes(bytes(hdr) + body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dol', nargs='?')
    ap.add_argument('--map')
    ap.add_argument('--seed', action='store_true', help='seed function discovery from bl targets + data pointers')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--name', default='basara')
    a = ap.parse_args()

    work = ROOT / 'generated' / 'w1' / ('selftest' if a.selftest else a.name)
    work.mkdir(parents=True, exist_ok=True)
    if a.selftest:
        dol = work / 'selftest.dol'
        make_selftest_dol(dol)
    elif a.dol:
        dol = Path(a.dol)
    else:
        ap.error('needs a main.dol path or --selftest')

    data = dol.read_bytes()
    secs, entry, bss_addr, bss_size = parse_dol(data)
    code = sum(s for _, _, s, t in secs if t)
    print('DOL: %s (%d bytes) | sections: %d | code: %d bytes | entry: %08X | bss: %08X+%X'
          % (dol.name, len(data), len(secs), code, entry, bss_addr, bss_size))
    sda, sda2, how = find_sda(data, secs, entry)
    print('SDA: r13=%s r2=%s (%s)' % ('%08X' % sda if sda else '-', '%08X' % sda2 if sda2 else '-', how))
    if sda is None:
        sys.exit('stopped: SDA bases not found, fill them in the manifest by hand')

    fmap = a.map
    if a.seed:
        fmap = work / 'seed_map.txt'
        seed_map(data, secs, fmap)
    manifest = work / 'manifest.yml'
    write_manifest(manifest, dol, sda, sda2, fmap)
    out = work / 'out'
    out.mkdir(exist_ok=True)
    cmd = [str(DOTNET), str(CLI), 'translate-recursive', '0x%08X' % entry,
           '--project', str(manifest), '--output-metadata', str(out / 'metadata.json')]
    env = dict(os.environ, DOTNET_ROOT=str(DOTNET.parent), DOTNET_NOLOGO='1', DOTNET_CLI_TELEMETRY_OPTOUT='1')
    t0 = time.time()
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors='replace')
    log = work / 'translate.log'
    log.write_text(r.stdout + '\n--- stderr ---\n' + r.stderr, encoding='utf-8')
    print('translate-recursive: exit %d, %.1f s, log: %s' % (r.returncode, time.time() - t0, log))

    gen = [p for p in out.rglob('*') if p.is_file()]
    print('result: %d files, %.1f MB in %s' % (len(gen), sum(p.stat().st_size for p in gen) / 1e6, out))
    md = out / 'metadata.json'
    if md.exists():
        j = json.loads(md.read_text(encoding='utf-8'))
        if isinstance(j, dict):
            print('metadata:', {k: (len(v) if isinstance(v, (list, dict)) else v) for k, v in j.items()})
    for ln in (r.stdout + r.stderr).strip().splitlines()[-12:]:
        print('  |', ln)


if __name__ == '__main__':
    main()
