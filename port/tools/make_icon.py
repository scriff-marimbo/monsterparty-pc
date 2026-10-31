#!/usr/bin/env python3
"""Makes the Windows .exe icon from the port's own art (Pillow needed).

    python3 port/tools/make_icon.py port/graphics/icon.png OUT.ico

The Windows build runs this itself when port/graphics/icon.png exists (it's the user's own drawing,
like the other PNGs there; never graphics from the ROM). A small square picture is best, e.g.
16x16 or 32x32 pixel art: each icon size is scaled from it with nearest-neighbour, so the pixels stay
sharp, and pictures that don't divide evenly are padded to a square with transparency.
"""
import sys
from PIL import Image

SIZES = [16, 24, 32, 48, 64, 128, 256]


def frame(src, size):
    w, h = src.size
    scale = max(1, size // max(w, h))
    if max(w, h) * scale > size:          # bigger than this icon size: shrink smoothly instead
        im = src.copy()
        im.thumbnail((size, size), Image.Resampling.LANCZOS)
    else:
        im = src.resize((w * scale, h * scale), Image.Resampling.NEAREST)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    out.paste(im, ((size - im.width) // 2, (size - im.height) // 2))
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src = Image.open(sys.argv[1]).convert('RGBA')
    frames = [frame(src, s) for s in SIZES]
    frames[-1].save(sys.argv[2], format='ICO', sizes=[(s, s) for s in SIZES], append_images=frames[:-1])


if __name__ == '__main__':
    main()
