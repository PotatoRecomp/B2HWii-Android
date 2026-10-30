"""Summarise a BASARA_PROFILE profile-stacks.txt (Android) with symbols.

Usage: python -I tools/b-stacks.py STACKS LIBMAIN [--top 25] [--scene HEX]
LIBMAIN is the unstripped generated/android-build/libmain.so of the same build.
Prints: innermost frames (self time), the first libmain frame of samples that sit in another module
(who calls libc etc.), and inclusive time per libmain function.
"""
import argparse
import collections
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("stacks")
parser.add_argument("libmain")
parser.add_argument("--top", type=int, default=25)
parser.add_argument("--scene", help="only samples with this scene (as written after S)")
parser.add_argument("--chains", metavar="MODULE",
                    help="also print the first 4 libmain frames of samples whose innermost frame is in MODULE")
parser.add_argument("--guest-self", action="store_true",
                    help="also count each sample against its innermost translated guest function (func_*)")
parser.add_argument("--children", metavar="FUNC",
                    help="inclusive time of each guest function FUNC (e.g. func_802BE4F4) calls directly, "
                         "and how much of it is inside GX")
args = parser.parse_args()
guest_samples = []

self_time = collections.Counter()
caller_of_foreign = collections.Counter()
chains = collections.Counter()
inclusive = collections.Counter()
total = 0
offsets = set()
with open(args.stacks) as f:
    for line in f:
        if line.startswith("#"):
            continue
        parts = line.split()
        if not parts or not parts[0].startswith("S"):
            continue
        if args.scene and parts[0][1:].upper() != args.scene.upper():
            continue
        frames = parts[1:]
        if not frames:
            continue
        total += 1
        inner = frames[0]
        is_main = "+" not in inner and inner != "?"
        self_time[inner if not is_main else int(inner, 16)] += 1
        if is_main:
            offsets.add(int(inner, 16))
        else:
            first = next((x for x in frames if "+" not in x and x != "?"), None)
            if first:
                caller_of_foreign[(inner.split("+")[0], int(first, 16))] += 1
                offsets.add(int(first, 16))
            if args.chains and inner.split("+")[0] == args.chains:
                chain = tuple(int(x, 16) for x in frames if "+" not in x and x != "?")[:4]
                chains[chain] += 1
        seen = set()
        for x in frames:
            if "+" not in x and x != "?":
                seen.add(int(x, 16))
        for x in seen:
            inclusive[x] += 1
        offsets.update(seen)
        if args.guest_self or args.children:
            guest_samples.append([int(x, 16) for x in frames if "+" not in x and x != "?"])

symbolizer = os.path.join(os.environ["LOCALAPPDATA"], "Android", "Sdk", "ndk", "28.2.13676358", "toolchains",
                          "llvm", "prebuilt", "windows-x86_64", "bin", "llvm-symbolizer.exe")
wanted = set()
for key, _ in self_time.most_common(args.top):
    if isinstance(key, int):
        wanted.add(key)
for (_, off), _ in caller_of_foreign.most_common(args.top):
    wanted.add(off)
for off, _ in inclusive.most_common(args.top * 2):
    wanted.add(off)
for chain, _ in chains.most_common(args.top):
    wanted.update(chain)
if args.guest_self or args.children:
    wanted.update(offsets)
wanted = sorted(wanted)
names = {}
if wanted:
    out = subprocess.run([symbolizer, "--obj=" + args.libmain, "--functions=short", "--no-inlines",
                          "--output-style=GNU"],
                         input="\n".join(hex(o) for o in wanted), capture_output=True, text=True).stdout
    lines = out.splitlines()
    for i, off in enumerate(wanted):
        names[off] = lines[2 * i] if 2 * i < len(lines) else "?"


def name(key):
    return key if isinstance(key, str) else names.get(key, hex(key))


print(f"{total} samples")
print("\n== self (innermost frame)")
for key, n in self_time.most_common(args.top):
    print(f"{100.0 * n / total:5.1f}%  {name(key)}")
print("\n== samples in another module, by first libmain frame")
for (module, off), n in caller_of_foreign.most_common(args.top):
    print(f"{100.0 * n / total:5.1f}%  {module} <- {name(off)}")
if args.chains:
    print(f"\n== libmain chains under {args.chains}")
    for chain, n in chains.most_common(args.top):
        print(f"{100.0 * n / total:5.1f}%  " + " <- ".join(name(x) for x in chain))
if args.guest_self:
    guest_self = collections.Counter()
    guest_inner = collections.Counter()
    for frames in guest_samples:
        guest = next((names.get(x, "") for x in frames if names.get(x, "").startswith("func_")), "(no guest)")
        guest_self[guest] += 1
        guest_inner[(guest, names.get(frames[0], "?") if frames else "?")] += 1
    print("\n== self by innermost guest function")
    for key, n in guest_self.most_common(args.top):
        print(f"{100.0 * n / total:5.1f}%  {key}")
    print("\n== innermost host frame under the busiest guest functions")
    for guest, _ in guest_self.most_common(5):
        for (g, inner), n in guest_inner.most_common():
            if g == guest and n * 200 >= total:
                print(f"{100.0 * n / total:5.1f}%  {g} > {inner}")
if args.children:
    child_time = collections.Counter()
    child_gx = collections.Counter()
    for frames in guest_samples:
        named = [names.get(x, "") for x in frames]
        if args.children not in named:
            continue
        # Frames are innermost first: the direct callee is the nearest guest function inside FUNC.
        outer = len(named) - 1 - named[::-1].index(args.children)
        child = next((n for n in reversed(named[:outer]) if n.startswith("func_")), "(self)")
        child_time[child] += 1
        if any(n.startswith("GX") or "aurora" in n for n in named[:outer]):
            child_gx[child] += 1
    print(f"\n== direct callees of {args.children} (inclusive, of which in GX)")
    for child, n in child_time.most_common(args.top):
        print(f"{100.0 * n / total:5.1f}%  {child}  (GX {100.0 * child_gx[child] / total:.1f}%)")
print("\n== inclusive (libmain)")
for off, n in inclusive.most_common(args.top * 2):
    print(f"{100.0 * n / total:5.1f}%  {name(off)}")
