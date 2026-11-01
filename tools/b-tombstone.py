"""Prints the crashing thread of an Android tombstone protobuf (last_crash_tombstone.pb).

Usage: python -I tools/b-tombstone.py last_crash_tombstone.pb [--all]
Minimal protobuf reader for system/core/debuggerd/proto/tombstone.proto (no protobuf package needed).
"""
import sys


def varint(b, i):
    v = s = 0
    while True:
        c = b[i]; i += 1
        v |= (c & 0x7F) << s; s += 7
        if c < 0x80:
            return v, i


def fields(b):
    i, out = 0, []
    while i < len(b):
        key, i = varint(b, i)
        f, t = key >> 3, key & 7
        if t == 0:
            v, i = varint(b, i)
        elif t == 1:
            v = int.from_bytes(b[i:i + 8], 'little'); i += 8
        elif t == 2:
            n, i = varint(b, i); v = b[i:i + n]; i += n
        elif t == 5:
            v = int.from_bytes(b[i:i + 4], 'little'); i += 4
        else:
            raise ValueError(f'wire type {t}')
        out.append((f, v))
    return out


def get(fs, f):
    return [v for k, v in fs if k == f]


def frame(fb):
    fs = fields(fb)
    g = lambda f, d=0: (get(fs, f) or [d])[0]
    name = g(4, b'').decode(errors='replace')
    off = g(5)
    lib = g(6, b'').decode(errors='replace').split('!')[-1].split('/')[-1]
    return f'  rel_pc {g(1):#010x}  {lib}  {name}{"+" + hex(off) if name else ""}'


def thread(tb):
    fs = fields(tb)
    tid = (get(fs, 1) or [0])[0]
    name = (get(fs, 2) or [b''])[0].decode(errors='replace')
    regs = {}
    for r in get(fs, 3):
        rf = fields(r)
        regs[(get(rf, 1) or [b''])[0].decode()] = (get(rf, 2) or [0])[0]
    return tid, name, regs, [frame(f) for f in get(fs, 4)]


def main():
    b = open(sys.argv[1], 'rb').read()
    top = fields(b)
    tid = (get(top, 5) or [0])[0]
    for f in (2, 3, 4):  # build fingerprint, revision, timestamp
        for v in get(top, f):
            print(v.decode(errors='replace') if isinstance(v, bytes) else v)
    for sig in get(top, 10):
        sf = fields(sig)
        print('signal', (get(sf, 1) or [0])[0], (get(sf, 2) or [b''])[0].decode(), (get(sf, 4) or [b''])[0].decode(),
              'addr', hex((get(sf, 9) or [0])[0]))
    for entry in get(top, 16):
        ef = fields(entry)
        t = thread((get(ef, 2) or [b''])[0])
        if t[0] == tid or '--all' in sys.argv:
            print(f'thread {t[0]} {t[1]}' + ('  <crashed>' if t[0] == tid else ''))
            if t[0] == tid:
                print('  pc', hex(t[2].get('pc', 0)), 'lr', hex(t[2].get('lr', t[2].get('x30', 0))))
            print('\n'.join(t[3]))


main()
