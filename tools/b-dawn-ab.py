"""Battle-block summary of a console.log (FPS log on): blocks with >= MIN draws.
Usage: python -I tools/b-dawn-ab.py MIN LOG [LOG...]"""
import re, sys
mn = int(sys.argv[1])
for path in sys.argv[2:]:
    rows, cur = [], None
    for l in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"\[vi\] fps ([\d.]+): game ([\d.]+) ms \(gx ([\d.]+) ms.*?(\d+) draws.*?worst frame ([\d.]+) ms, jitter ([\d.]+) ms", l)
        if m:
            cur = [float(x) for x in m.groups()] + [None, None]; rows.append(cur); continue
        m = re.search(r"render: worker ([\d.]+) ms a cycle \(prepare ([\d.]+)\).*?game waited ([\d.]+)", l)
        if m and cur is not None and cur[6] is None:
            cur[6], cur[7] = float(m[1]), float(m[3])
    sel = [r for r in rows if r[3] >= mn]
    if not sel:
        print(f"{path}: no block >= {mn} draws"); continue
    avg = lambda i: sum(r[i] for r in sel if r[i] is not None) / max(1, sum(r[i] is not None for r in sel))
    print(f"{path}: n={len(sel)} fps {avg(0):.1f} game {avg(1):.2f} gx {avg(2):.2f} draws {avg(3):.0f} "
          f"worst {avg(4):.1f} jitter {avg(5):.2f} worker {avg(6):.2f} wait {avg(7):.2f}")
