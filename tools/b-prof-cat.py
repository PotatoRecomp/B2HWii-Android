#!/usr/bin/env python3
"""Self time of a BASARA_PROFILE profile-stacks.txt by category (wait, memory helpers, dispatch,
GX HLE, guest code...), every sample symbolized. Usage:
  python -I tools/b-prof-cat.py STACKS LIBMAIN [--names 40]
"""
import argparse
import collections
import os
import re
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument("stacks")
ap.add_argument("libmain")
ap.add_argument("--names", type=int, default=40)
args = ap.parse_args()

samples = []  # innermost frame and the first libmain frame below a foreign one
with open(args.stacks, encoding="utf-8", errors="replace") as f:
    for line in f:
        if not line.startswith("S"):
            continue
        frames = line.split()[1:]
        if not frames:
            continue
        inner = frames[0]
        first_own = next((x for x in frames if "+" not in x and x != "?"), None)
        samples.append((inner, first_own))

offsets = sorted({int(x, 16) for s in samples for x in s if x and "+" not in x and x != "?"})
symbolizer = os.path.join(os.environ["LOCALAPPDATA"], "Android", "Sdk", "ndk", "28.2.13676358", "toolchains",
                          "llvm", "prebuilt", "windows-x86_64", "bin", "llvm-symbolizer.exe")
out = subprocess.run([symbolizer, "--obj=" + args.libmain, "--functions=short", "--no-inlines",
                      "--output-style=GNU"], input="\n".join(hex(o) for o in offsets),
                     capture_output=True, text=True).stdout.splitlines()
names = {off: out[2 * i] if 2 * i < len(out) else "?" for i, off in enumerate(offsets)}

RULES = [
    ("guest code (func_*)", r"^func_[0-9A-F]{8}$"),
    ("GX HLE / FIFO", r"GX|Gx|gx_|write_u8|write_u16|write_u32|write_data|Aurora|XFArray|VtxState|DlScan|DisplayList|Fifo|FIFO"),
    ("memory helpers", r"Flat(Read|Write)|TryGet(Writable)?PointerFast|TryReadGuest|TryWriteGuest|ByteSwap|Read(8|16|32|64)$|Write(8|16|32|64)$|NeedsPolicy|Canonicalize|DcRangeOp"),
    ("indirect dispatch", r"Dispatch|FindByAddress|FindRawByAddress|Invoke|Resolved|StaticIndirect"),
    ("register guards", r"Guard"),
    ("paired single / float", r"Ppc|PPC_|Psq|Float|Double|Single"),
    ("OS/HLE scheduler", r"Thread|Sleep|Timer|Alarm|Scheduler|Fiber|OS"),
    ("audio", r"Audio|AX|Dsp|DSP"),
]

cats = collections.Counter()
by_name = collections.Counter()
total = len(samples)
for inner, first_own in samples:
    if "+" in inner or inner == "?":
        module = inner.split("+")[0]
        caller = names.get(int(first_own, 16), "?") if first_own else "?"
        if module == "libc.so" and re.search(r"sleep|future_error|wait|Sleep", caller):
            cat = "wait / sleep (libc)"
        elif module in ("libc.so", "[vdso]"):
            cat = "libc/vdso: " + ("memcpy-like" if module == "libc.so" else "clock")
        else:
            cat = "other modules"
        by_name[f"{module} <- {caller}"] += 1
    else:
        n = names.get(int(inner, 16), "?")
        by_name[n] += 1
        cat = next((c for c, rx in RULES if re.search(rx, n)), "other runtime")
    cats[cat] += 1

print(f"{total} samples\n== by category")
for c, n in cats.most_common():
    print(f"{100.0 * n / total:5.1f}%  {c}")
print(f"\n== top {args.names} names (self)")
for k, n in by_name.most_common(args.names):
    print(f"{100.0 * n / total:5.1f}%  {k}")
