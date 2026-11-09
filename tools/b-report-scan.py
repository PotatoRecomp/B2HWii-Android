"""Summarise 1.2-style tester console.log files (no periodic fps line).

Usage: python -I tools/b-report-scan.py LOG [LOG ...]
Per log: device/renderer/settings, slow-frame stats (wall vs game-thread cpu),
vkgpu gpu ms, dynamic resolution / frameskip / thermal / fatal events.
"""
import re
import statistics
import sys


def pct(values, p):
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int(len(values) * p))]


def scan(path):
    head = {}
    slow_wall, slow_cpu, slow_gx, slow_disc, slow_present = [], [], [], [], []
    gpu, gpu_draws = [], []
    events = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("[runtime] device:") and "device" not in head:
                head["device"] = line[18:120]
            elif line.startswith("[runtime] settings:") and "settings" not in head:
                head["settings"] = line[20:]
            elif "[aurora::gpu] Renderer:" in line and "renderer" not in head:
                head["renderer"] = line.split("Renderer:", 1)[1].strip()
            elif line.startswith("[vi] display: aspect") and "display" not in head:
                head["display"] = line[14:]
            elif line.startswith("[vkgpu] device:") and "vkdev" not in head:
                head["vkdev"] = line[16:140]
            m = re.search(r"slow frame ([\d.]+) ms: game ([\d.]+) \(gx ([\d.]+), disc ([\d.]+).*?present ([\d.]+).*?thread cpu ([\d.]+) user \+ ([\d.]+) kernel", line)
            if m:
                slow_wall.append(float(m[2]))
                slow_gx.append(float(m[3]))
                slow_disc.append(float(m[4]))
                slow_present.append(float(m[5]))
                slow_cpu.append(float(m[6]) + float(m[7]))
                continue
            m = re.search(r"\[vkgpu\] gpu: ([\d.]+) ms a frame.*?with (\d+) draws", line)
            if m:
                gpu.append(float(m[1]))
                gpu_draws.append(int(m[2]))
                continue
            if line.startswith("[vi] summary"):
                events.append(line[:200])
                continue
            if re.search(r"dynamic resolution:|frameskip:|\[runtime\] thermal:|fatal|Fatal|SIGSEGV|SIGABRT|abort|device lost|DeviceLost|out of memory", line):
                events.append(line[:200])
    print(f"== {path}")
    for k in ("device", "renderer", "vkdev", "settings", "display"):
        if k in head:
            print(f"  {k}: {head[k]}")
    if slow_wall:
        waits = [w - c for w, c in zip(slow_wall, slow_cpu)]
        print(f"  slow frames {len(slow_wall)}: game med {statistics.median(slow_wall):.1f} p90 {pct(slow_wall, .9):.1f} | "
              f"thread cpu med {statistics.median(slow_cpu):.1f} | not-on-cpu med {statistics.median(waits):.1f} | "
              f"gx med {statistics.median(slow_gx):.1f} p90 {pct(slow_gx, .9):.1f} | disc>5ms {sum(d > 5 for d in slow_disc)} | present med {statistics.median(slow_present):.1f}")
    if gpu:
        print(f"  vkgpu gpu {len(gpu)} samples: med {statistics.median(gpu):.1f} p90 {pct(gpu, .9):.1f} max {max(gpu):.1f} ms, draws med {statistics.median(gpu_draws):.0f}")
    seen = {}
    for e in events:
        key = re.sub(r"[\d.]+", "#", e)[:70]
        seen[key] = seen.get(key, 0) + 1
        if seen[key] <= 3:
            print(f"  ev: {e}")
    extra = {k: v for k, v in seen.items() if v > 3}
    for k, v in extra.items():
        print(f"  ev x{v}: {k}")


for p in sys.argv[1:]:
    scan(p)
