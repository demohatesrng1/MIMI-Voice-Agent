#!/usr/bin/env python3
"""Build assets/Mimi.icns from a source illustration.

Follows Apple's macOS icon grid rather than just scaling a square: on a 1024pt
canvas the artwork sits in an 824pt rounded rectangle with a ~185pt corner
radius, leaving the margin the system expects for shadows and alignment. A
full-bleed square icon is the usual tell that an app is not native.

    python3 scripts/make_icon.py ~/Desktop/app.jpg

Run once; the resulting .icns is committed. Needs Pillow, which is not a
dependency of the app itself.
"""

import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

CANVAS = 1024
ART = 824               # Apple's artwork square inside the canvas
RADIUS = 185            # corner radius at 1024pt
SUPERSAMPLE = 4         # mask is built big and downscaled, for clean edges

# name -> (pixel size) for the .iconset macOS expects
SIZES = {
    "icon_16x16.png": 16,       "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32,       "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128,    "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256,    "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512,    "icon_512x512@2x.png": 1024,
}


def square_crop(image, focus=0.5):
    """Crop to a square. `focus` is where vertically to centre it, 0=top.

    Portrait character art puts the face near the top, so a plain centre crop
    usually decapitates it.
    """
    width, height = image.size
    side = min(width, height)
    if height > width:
        top = int((height - side) * focus)
        return image.crop((0, top, side, top + side))
    left = (width - side) // 2
    return image.crop((left, 0, left + side, side))


def rounded_mask(size, radius):
    """An antialiased rounded-rectangle mask."""
    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, big - 1, big - 1), radius=radius * SUPERSAMPLE, fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def build(source_path, out_dir):
    source = Image.open(source_path).convert("RGB")
    # Bias upward: the face is in the top third of character art.
    art = square_crop(source, focus=0.06).resize((ART, ART), Image.LANCZOS)

    art.putalpha(rounded_mask(ART, RADIUS))

    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    offset = (CANVAS - ART) // 2

    # Soft contact shadow, sitting slightly low, the way macOS icons do.
    shadow = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 90), (offset, offset + 10), art.split()[3])
    canvas = Image.alpha_composite(canvas, shadow.filter(ImageFilter.GaussianBlur(14)))

    canvas.alpha_composite(art, (offset, offset))

    iconset = os.path.join(out_dir, "Mimi.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)
    for name, size in SIZES.items():
        canvas.resize((size, size), Image.LANCZOS).save(
            os.path.join(iconset, name), "PNG")

    icns = os.path.join(out_dir, "Mimi.icns")
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
    shutil.rmtree(iconset, ignore_errors=True)

    # A plain PNG too, for the in-app window icon.
    canvas.resize((512, 512), Image.LANCZOS).save(
        os.path.join(out_dir, "mimi_512.png"), "PNG")
    return icns


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: make_icon.py SOURCE_IMAGE [OUT_DIR]")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "assets"
    os.makedirs(out_dir, exist_ok=True)
    icns = build(sys.argv[1], out_dir)
    print(f"{icns}  ({os.path.getsize(icns) / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
