"""List bl call sites of a function in the Basara main.dol: python -I tools/b-callers.py TARGET [TARGET...]"""
import struct, sys
from pathlib import Path
d = Path(__file__).resolve().parent.parent.joinpath('assets/basara-heroes/sys/main.dol').read_bytes()
offs = struct.unpack('>18I', d[0:72]); addrs = struct.unpack('>18I', d[72:144]); sizes = struct.unpack('>18I', d[144:216])
targets = {int(a, 16) for a in sys.argv[1:]}
for i in range(7):
    for k in range(0, sizes[i], 4):
        w = struct.unpack('>I', d[offs[i] + k:offs[i] + k + 4])[0]
        if w >> 26 == 18 and w & 3 == 1:
            li = w & 0x3FFFFFC
            if li & 0x2000000:
                li -= 0x4000000
            a = addrs[i] + k
            if (a + li) & 0xFFFFFFFF in targets:
                print(f'{a:08X} -> {(a + li) & 0xFFFFFFFF:08X}')
