# Draws the app icon from the tree artwork: the ink of Tools/art/tree_source.png
# in cream with a soft gold glow, on a rounded tile in the reverse pedal's sage
# (the colours of UI/style.css). Writes Resources/icon.png, which CMakeLists.txt
# hands to JUCE as the standalone app's and the plugin's icon.
#
# Needs numpy and Pillow. Run from the repo root:
#     python Tools/make_icon.py
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Tools" / "art" / "tree_source.png"
OUT = ROOT / "Resources" / "icon.png"

SIZE = 1024
SS = 2                  # drawn at twice the size, then scaled down for clean edges
INK = 175               # darker than this is ink (as in trace_tree.py)
MARGIN = 0.035          # transparent border around the tile, fraction of the size
RADIUS = 0.2            # tile corner radius, fraction of the tile
TREE_H = 0.78           # tree height, fraction of the tile

REV = (0x6f, 0x8a, 0x74)        # --rev
REV_DEEP = (0x33, 0x44, 0x3a)   # --rev-deep
DARK = (0x12, 0x14, 0x18)       # --bg
INK_RGB = (0xec, 0xe4, 0xcf)    # --ink
GOLD = (0xd8, 0xb7, 0x65)       # --gold

def lerp(a, b, t):
    return a + (b - a) * t

def tree_mask():
    """The drawing's ink as an alpha mask, cropped to the tree. Soft edges come
    from the grey ramp just above the threshold; the pale wash under the grass
    stays out."""
    g = np.asarray(Image.open(SRC).convert("L"), np.float32)
    a = np.clip((INK + 40 - g) / 40, 0, 1)
    ys, xs = np.nonzero(a > 0.5)
    a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return Image.fromarray((a * 255).astype(np.uint8))

def main():
    N = SIZE * SS
    m = round(N * MARGIN)
    tile = N - 2 * m

    # Tile: the pedal's diagonal sage gradient, darkening towards the bottom.
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N
    t = np.clip(0.15 + 0.55 * yy + 0.25 * xx, 0, 1)[..., None]
    bg = lerp(np.array(REV, np.float32), np.array(REV_DEEP, np.float32), t)
    # A dark vignette so the tree's glow carries.
    r = np.hypot(xx - 0.5, (yy - 0.45) * 1.1)[..., None]
    bg = lerp(bg, np.array(DARK, np.float32), np.clip((r - 0.25) * 1.1, 0, 0.55))
    img = Image.fromarray(bg.astype(np.uint8), "RGB").convert("RGBA")

    # Tree, centred, its foot a little above the tile's bottom.
    tree = tree_mask()
    h = round(tile * TREE_H)
    w = round(tree.width * h / tree.height)
    tree = tree.resize((w, h), Image.LANCZOS)
    layer = Image.new("L", (N, N), 0)
    layer.paste(tree, ((N - w) // 2, m + round(tile * 0.1)))

    # Glow: a wide faint halo and a tighter bright one, in gold.
    for blur, gain in ((N * 0.03, 1.6), (N * 0.008, 1.3)):
        a = layer.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: min(255, int(v * gain)))
        img = Image.alpha_composite(img, Image.merge("RGBA", (*[Image.new("L", (N, N), c) for c in GOLD], a)))
    # The tree itself in cream.
    img = Image.alpha_composite(img, Image.merge("RGBA", (*[Image.new("L", (N, N), c) for c in INK_RGB], layer)))

    # Cut to the rounded tile.
    shape = Image.new("L", (N, N), 0)
    ImageDraw.Draw(shape).rounded_rectangle((m, m, N - m - 1, N - m - 1), radius=round(tile * RADIUS), fill=255)
    img.putalpha(ImageChops.multiply(img.getchannel("A"), shape))

    OUT.parent.mkdir(exist_ok=True)
    img.resize((SIZE, SIZE), Image.LANCZOS).save(OUT, optimize=True)
    print(f"wrote {OUT.relative_to(ROOT)}")

if __name__ == "__main__":
    main()
