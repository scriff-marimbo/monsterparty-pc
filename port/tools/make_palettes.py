#!/usr/bin/env python3
"""Write the palette files bundled with the port (port/palettes/*.pal, JASC-PAL).

usage: make_palettes.py OUT_DIR

They're all made from the same NTSC signal model as the built-in palette (src/palette.cpp),
with different settings or a colour treatment on top. JASC-PAL is the Paint Shop Pro palette
format that most image and palette editors open: "JASC-PAL", "0100", the colour count, then one
"R G B" line per colour. The first 64 entries are the NES colours $00-$3F in order; the file name
is the palette's name in the settings window. Edit them freely afterwards; this script is only a
starting point. Your own palettes belong in
~/.config/monsterparty/palettes/ (the settings window's "Open my palettes folder").
"""
import math, os, sys

LEVELS = [.350, .518, .962, 1.550, 1.094, 1.506, 1.962, 1.962]
BLACK, WHITE = .518, 1.962


def ntsc_yiq(c):
    """Y, I, Q of NES colour c (I/Q not yet scaled by saturation); None for the blacks $xE/$xF."""
    hue, lvl = c & 15, (c >> 4) & 3
    if hue >= 0xE:
        return None
    lo = LEVELS[lvl + 4 * (hue == 0)]
    hi = LEVELS[lvl + 4 * (hue < 0xD)]
    y = i = q = 0.0
    for p in range(12):
        spot = hi if (hue + p + 8) % 12 < 6 else lo
        spot = (spot - BLACK) / (WHITE - BLACK)
        y += spot
        i += spot * math.cos(math.pi * p / 6)
        q += spot * math.sin(math.pi * p / 6)
    return y / 12, i / 12, q / 12


def yiq_rgb(y, i, q):
    return (y + 0.946882 * i + 0.623557 * q,
            y - 0.274788 * i - 0.635691 * q,
            y - 1.108545 * i + 1.709007 * q)


def clamp(v):
    return min(1.0, max(0.0, v))


def ntsc(saturation=1.5, hue_deg=0.0):
    """64 (r, g, b) floats 0-1 from the signal model."""
    out = []
    a = math.radians(hue_deg)
    for c in range(64):
        yiq = ntsc_yiq(c)
        if yiq is None:
            out.append((0.0, 0.0, 0.0))
            continue
        y, i, q = yiq
        i, q = i * math.cos(a) - q * math.sin(a), i * math.sin(a) + q * math.cos(a)
        out.append(tuple(clamp(v) for v in yiq_rgb(y, i * saturation, q * saturation)))
    return out


def luma(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def hexrgb(h):
    return tuple(int(h[k:k + 2], 16) / 255 for k in (0, 2, 4))


# ---- the palettes: (file, description, colours). The file name is the name shown in the settings window.
def vivid():
    return ntsc(saturation=2.0)


def soft():
    return [tuple(0.05 + 0.88 * v for v in c) for c in ntsc(saturation=1.15)]


def warm_crt():
    out = []
    for c in ntsc(saturation=1.55, hue_deg=4):
        r, g, b = (v ** 0.92 for v in c)          # a little brighter in the mid tones
        out.append((clamp(r * 1.0), clamp(g * 0.95), clamp(b * 0.84)))
    return out


def grayscale():
    return [(luma(c),) * 3 for c in ntsc()]


def sepia():
    dark, light = hexrgb('2B1A0C'), hexrgb('F4E3C1')
    return [mix(dark, light, luma(c) ** 0.85) for c in ntsc()]


def green_phosphor():
    glow = hexrgb('41FF6E')
    return [mix((0.0, 0.03, 0.0), glow, luma(c) ** 0.9) for c in ntsc()]


def four_shade_green():
    shades = [hexrgb(h) for h in ('0F380F', '306230', '8BAC0F', '9BBC0F')]
    return [shades[0 if l < 0.18 else 1 if l < 0.42 else 2 if l < 0.68 else 3]
            for l in (luma(c) for c in ntsc())]


PALETTES = [
    ('Vivid.pal', 'More saturated than the default.', vivid),
    ('Soft.pal', 'Less saturated, with lifted blacks: gentler on the eyes.', soft),
    ('Warm CRT.pal', 'Warmer white point and hue, a little brighter, like an old TV.', warm_crt),
    ('Grayscale.pal', 'Black and white, from each colour\'s brightness.', grayscale),
    ('Sepia.pal', 'Brown-toned, like an old photograph.', sepia),
    ('Green phosphor.pal', 'A green monochrome monitor.', green_phosphor),
    ('Four-shade green.pal', 'Only four greens, like an early handheld\'s screen.', four_shade_green),
]


def write(path, colours):
    with open(path, 'w', newline='\n') as f:
        f.write('JASC-PAL\n0100\n64\n')
        for c in colours:
            r, g, b = (int(v * 255 + 0.5) for v in c)
            f.write(f'{r} {g} {b}\n')


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for file, desc, fn in PALETTES:
        write(os.path.join(out, file), fn())
        print(f'{file}: {desc}')


if __name__ == '__main__':
    main()
