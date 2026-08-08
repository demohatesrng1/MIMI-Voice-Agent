#!/usr/bin/env python3
"""Turn the portrait artwork into the square faces the app actually draws.

The source images are 2:3 portraits of the whole character. Everywhere she
appears as a face -- the orb, the always-on-top puck, the picker -- she is
drawn inside a *circle*, so what the app needs is a square with her head
comfortably inside it.

A centre crop is wrong: on a 2:3 portrait the middle of the frame is her chest.
Her face sits between 37% and 40% of the height in all six images (measured,
not guessed), so a square the full width of the image, offset 8% down from the
top, lands her eyes at about 45% of the crop -- slightly above centre, which is
where a portrait wants them.

Re-run after adding or replacing artwork in the source folder:

    python3 scripts/build_faces.py [source_dir]
"""

import sys
import pathlib
from PIL import Image

# Where the crop is taken from, as a fraction of the source height. Faces sit
# at 0.37-0.40; this puts them just above the middle of the square.
TOP_FRACTION = 0.08
# Big enough for a Retina settings preview; the orb masks it down from here.
OUTPUT_SIZE = 640

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEST = ROOT / "assets" / "faces"


def main() -> int:
    source = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                          else pathlib.Path.home() / "Desktop" / "pfp")
    if not source.is_dir():
        print(f"no such folder: {source}", file=sys.stderr)
        return 1

    images = sorted(p for p in source.iterdir()
                    if p.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp"})
    if not images:
        print(f"no images in {source}", file=sys.stderr)
        return 1

    DEST.mkdir(parents=True, exist_ok=True)
    for index, path in enumerate(images, start=1):
        image = Image.open(path).convert("RGB")
        side = min(image.width, image.height)
        top = int(image.height * TOP_FRACTION)
        # Never run off the bottom on a source that is closer to square.
        top = min(top, image.height - side)
        left = (image.width - side) // 2

        face = image.crop((left, top, left + side, top + side))
        face = face.resize((OUTPUT_SIZE, OUTPUT_SIZE), Image.LANCZOS)
        out = DEST / f"face{index}.png"
        face.save(out, optimize=True)
        print(f"  {path.name}  ->  assets/faces/{out.name}"
              f"  ({out.stat().st_size // 1024} KB)")

    print(f"\n{len(images)} faces written to {DEST}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
