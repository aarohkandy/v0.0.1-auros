#!/usr/bin/env python3
"""The website's pictures of AurOS Setup: crops of its own screens, and where
their numbered rings go.

    python3 tools/site/pictures.py

Reads docs/shots/wizard/hidpi-*.png (stream B's renders of the Light Setup at
1.5x), keeps the page and drops the step list on the left, writes
website/assets/shots/setup-*.jpg, and prints each ring as the left / top /
width / height percentages that website/index.html's .ring elements carry.
Rings are found by colour, not by hand: the green button (#0B5D4F), and the
chosen look's card fill (#E1ECE7). Run it again whenever B re-renders.
"""
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "docs", "shots", "wizard")
OUT = os.path.join(REPO, "website", "assets", "shots")
GO, SELECTED = (0x0B, 0x5D, 0x4F), (0xE1, 0xEC, 0xE7)
KEEP_FROM_X = 420          # 280 px of step list, at 1.5x


def box_of(im, rgb, region, slack=6):
    """Bounding box of the pixels of one colour inside region (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = region
    px = im.load()
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px[x, y]
            if all(abs(p[i] - rgb[i]) <= slack for i in range(3)):
                xs.append(x)
                ys.append(y)
    if not xs:
        sys.exit("pictures.py: no pixels of colour %r in %r" % (rgb, region))
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def pct(b, crop, pad=4):
    """A box inside a crop, as the percentages the page's .ring takes."""
    cx0, cy0, cx1, cy1 = crop
    w, h = cx1 - cx0, cy1 - cy0
    x0, y0, x1, y1 = b[0] - pad, b[1] - pad, b[2] + pad, b[3] + pad
    return "left:%.1f%%;top:%.1f%%;width:%.1f%%;height:%.1f%%" % (
        100.0 * (x0 - cx0) / w, 100.0 * (y0 - cy0) / h,
        100.0 * (x1 - x0) / w, 100.0 * (y1 - y0) / h)


def main():
    for name, out, rings in [
        ("hidpi-1c-welcome-ready.png", "setup-check.jpg", ["button"]),
        ("hidpi-3a-style.png", "setup-look.jpg", ["card", "button"]),
        ("hidpi-4a-ready.png", "setup-ready.jpg", ["button"]),
    ]:
        im = Image.open(os.path.join(SRC, name)).convert("RGB")
        w, h = im.size
        crop = (KEEP_FROM_X, 0, w, h)
        im.crop(crop).save(os.path.join(OUT, out), "JPEG", quality=84, optimize=True, progressive=True)
        print("%s: %dx%d" % (out, crop[2] - crop[0], crop[3] - crop[1]))
        for r in rings:
            if r == "button":      # the primary button: bottom right
                b = box_of(im, GO, (w // 2, int(h * 0.8), w, h))
            else:                  # the chosen look's card
                b = box_of(im, SELECTED, (KEEP_FROM_X, int(h * 0.1), w, int(h * 0.5)))
            print("  ring %-6s %s" % (r, pct(b, crop)))


if __name__ == "__main__":
    main()
