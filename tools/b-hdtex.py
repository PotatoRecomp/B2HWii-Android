"""Builds an HD texture pack from the disc extraction for the textures a play session actually used.

For every PNG in DUMP (runtime dumps, names = aurora replacement keys) the same-named disc texture is
taken from ISO (tools/b-tpl-extract.py output), upscaled SCALE times and written to OUT together with
a full mip chain (<name>_mip1.png ...), so the bigger texture does not shimmer at low render scales.

Upscaler: Real-ESRGAN ncnn (--esrgan EXE) when given, else Lanczos + unsharp mask (Pillow).
Alpha is upscaled separately with Lanczos so cut-out edges stay clean.

Usage: python -I tools/b-hdtex.py DUMP OUT [--iso generated/textures-iso] [--scale 2] [--esrgan EXE]
"""
import argparse
import os
import subprocess
import tempfile

from PIL import Image, ImageFilter


def index_iso(root):
    names = {}
    for d, _, files in os.walk(root):
        for f in files:
            if f.startswith("tex1_") and f.endswith(".png"):
                names.setdefault(f, os.path.join(d, f))
    return names


def upscale_pillow(img, scale):
    w, h = img.size
    big = img.resize((w * scale, h * scale), Image.LANCZOS)
    rgb = big.convert("RGB").filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))
    out = rgb.convert("RGBA")
    out.putalpha(big.getchannel("A"))
    return out


def upscale_esrgan(exe, src_dir, dst_dir, scale):
    model = "realesr-animevideov3"
    models = os.path.join(os.path.dirname(os.path.abspath(exe)), "models")
    subprocess.run([exe, "-i", src_dir, "-o", dst_dir, "-m", models, "-n", model, "-s", str(scale), "-f", "png"],
                   check=True, stdout=subprocess.DEVNULL)


def write_with_mips(img, path):
    img.save(path, optimize=False, compress_level=3)
    stem, ext = os.path.splitext(path)
    level, cur = 1, img
    while cur.width > 1 or cur.height > 1:
        cur = cur.resize((max(cur.width // 2, 1), max(cur.height // 2, 1)), Image.LANCZOS)
        cur.save(f"{stem}_mip{level}{ext}", compress_level=3)
        level += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("out")
    ap.add_argument("--iso", default="generated/textures-iso")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--esrgan")
    ap.add_argument("--min-size", type=int, default=32, help="skip textures smaller than this on both sides")
    a = ap.parse_args()

    iso = index_iso(a.iso)
    wanted = []
    for f in sorted(os.listdir(a.dump)):
        if not f.startswith("tex1_") or "_mip" in f or not f.endswith(".png"):
            continue
        if "_512x448_" in f:  # movie frames
            continue
        src = iso.get(f)
        if src is None:
            continue
        w, h = map(int, f.split("_")[1].split("x"))
        if w < a.min_size and h < a.min_size:
            continue
        wanted.append((f, src))
    print(f"{len(wanted)} textures to upscale ({a.scale}x)")
    os.makedirs(a.out, exist_ok=True)

    if a.esrgan:
        with tempfile.TemporaryDirectory() as tin, tempfile.TemporaryDirectory() as tout:
            for f, src in wanted:
                Image.open(src).convert("RGB").save(os.path.join(tin, f))
            upscale_esrgan(a.esrgan, tin, tout, a.scale)
            for f, src in wanted:
                orig = Image.open(src).convert("RGBA")
                up = Image.open(os.path.join(tout, f)).convert("RGBA")
                alpha = orig.getchannel("A").resize(up.size, Image.LANCZOS)
                up.putalpha(alpha)
                write_with_mips(up, os.path.join(a.out, f))
    else:
        for f, src in wanted:
            write_with_mips(upscale_pillow(Image.open(src).convert("RGBA"), a.scale), os.path.join(a.out, f))
    total = sum(os.path.getsize(os.path.join(a.out, f)) for f in os.listdir(a.out))
    print(f"done: {a.out} ({total / 1e6:.1f} MB on disk)")


if __name__ == "__main__":
    main()
