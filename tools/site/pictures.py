#!/usr/bin/env python3
"""The website's pictures of AurOS: made from the project's own renders, and tied to them.

    python3 tools/site/pictures.py

Writes website/assets/shots/*.jpg and website/assets/shots/pictures.json.

  setup-*.jpg  crops of stream B's renders of AurOS Setup
               (docs/shots/wizard/hidpi-*.png: the installer's own paint code,
               run under Wine on a test computer; NOT photographs of a real
               Windows screen, and the page says so until that changes)
  look-*.jpg   stream C's renders of the four looks, 1366x768
               (docs/shots/shells/<look>-1366x768-desktop.png: the shipping
               shell code, Light theme, the programs build/forge ships)

pictures.json records, for each picture, the file it was made from and that
file's SHA-256, what the picture SAYS (every line of text in it, as written in
the source), and where its numbered rings go (found by colour: the green
button #0B5D4F, the chosen look's card fill #E1ECE7). tools/site/check.mjs then
fails when:
  - a source render was replaced and this was not run again (the hash),
  - a line a picture shows is no longer a string in the installer or the shell
    (the words changed under the picture),
  - Ready now says something the picture does not (a row was added).
When B retakes the Setup pictures on a real Windows PC, set REAL_WINDOWS below
to True: the page's "made on a test computer" note must then go.

If you change a picture's text, change its SAYS here: the check reads them.
"""
import hashlib
import json
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "website", "assets", "shots")
GO, SELECTED = (0x0B, 0x5D, 0x4F), (0xE1, 0xEC, 0xE7)
KEEP_FROM_X = 420          # 280 px of step list, at 1.5x

# Set to True only when the three Setup pictures are screenshots of AurOS Setup
# on a real Windows PC (docs/shots/wizard retaken by B, P2-18).
# Ready was refreshed from hosted Windows run 40161803540, source 6a732d79,
# artifact wizard-shots-windows. The other Setup renders remain Wine captures;
# this mixed set is not physical-PC evidence.
REAL_WINDOWS = False

# Every line of text each picture shows, as the source writes it.
SAYS = {
    "setup-check.jpg": [
        "Let’s put AurOS on this computer",
        "AurOS is a free, simple new way to use this computer. AurOS is made to keep Windows and your files. If AurOS isn’t for you, you can go back to Windows.",
        "This computer can take AurOS.",
        "Next, you pick how it looks.",
        "AurOS, as it will look beside Windows. You pick the look next.",
        "Licenses",
        "Next",
    ],
    "setup-look.jpg": [
        "Pick the look you want",
        "Like Windows",
        "Like a Mac",
        "Big and simple",
        "One thing at a time",
        "Chosen",
        "Picked for you:",
        "Licenses",
        "Back",
        "Next",
    ],
    "setup-ready.jpg": [
        "Ready to install",
        "Windows and your files are kept. Setup makes Windows smaller to fit AurOS. If the power fails or the computer crashes during that step, Windows or your files could be damaged and might not come back.",
        "Any change to a drive carries some risk. Copy anything you can’t replace to a memory stick first.",
        "Programs made for Windows don’t run in AurOS.",
        "It will look like Windows: a bar along the bottom.",
        "AurOS downloads while you wait, so stay connected to the internet.",
        "The computer restarts once. Then AurOS asks if everything works. Press “No, go back to Windows” and it does.",
        "About 25 to 60 minutes total; the screen is dark for a minute or two.",
        "Setup turns off Fast Startup and hibernation, and leaves a Windows task, “AurOS put-back”, that does nothing unless AurOS is removed.",
        "By pressing Install AurOS you agree to the AurOS Terms.",
        "Read the Terms",
        "Licenses",
        "Back",
        "Install AurOS",
    ],
    "look-taskbar.jpg": [
        "All my programs", "Everything on this computer",
        "Internet", "Websites, news and shopping", "Photos", "Look at your pictures",
        "Your files", "Everything you have saved", "Documents", "Read PDFs and documents",
        "Music and video", "Play a song or a film", "Notes", "Write and read plain notes",
        "Calculator", "Add up, and work things out", "Zip files", "Open or make a zip file",
        "Printers", "Set up or check a printer", "Settings", "Change how this computer works",
        "Get more programs", "Find and add programs", "Nothing open yet.",
        "Help", "Wi-Fi", "Smaller text", "Bigger text", "Turn off",
    ],
    "look-dock.jpg": [
        "All my programs", "Internet", "Your files", "Documents", "Notes", "Get more programs",
        "Help", "Wi-Fi", "Settings", "Smaller text", "Bigger text", "Turn off",
    ],
    "look-rail.jpg": [
        "What would you like to do?", "Pick one. You can always come back here.",
        "Internet", "Websites, news and shopping", "Photos", "Look at your pictures",
        "Your files", "Everything you have saved", "Documents", "Read PDFs and documents",
        "All my programs", "Everything on this computer", "Get more programs", "Find and add programs",
        "Help", "Wi-Fi", "Settings", "Smaller text", "Bigger text", "Turn off",
    ],
    "look-tiles.jpg": [
        "What would you like to do?", "Previous page", "Next page",
        "Internet", "Websites, news and shopping", "Photos", "Look at your pictures",
        "Your files", "Everything you have saved", "Documents", "Read PDFs and documents",
        "Music and video", "Play a song or a film", "Notes", "Write and read plain notes",
        "Get more programs", "Find and add programs", "Home",
        "Help", "Wi-Fi", "Settings", "Smaller text", "Bigger text", "Turn off",
    ],
}

SETUP = [
    ("setup-check.jpg", "hidpi-1c-welcome-ready.png", ["button"]),
    ("setup-look.jpg", "hidpi-3a-style.png", ["card", "button"]),
    ("setup-ready.jpg", "hidpi-4a-ready.png", ["button"]),
]
LOOKS = [
    ("look-taskbar.jpg", "taskbar-1366x768-desktop.png"),
    ("look-dock.jpg", "dock-1366x768-desktop.png"),
    ("look-rail.jpg", "rail-1366x768-desktop.png"),
    ("look-tiles.jpg", "tiles-1366x768-desktop.png"),
]


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def box_of(im, rgb, region, slack=6, solid=4):
    """Bounding box of the pixels of one colour inside region (x0, y0, x1, y1).
    A pixel counts only if the pixels `solid` away in the four directions have
    the colour too: the edges of dark text on paper pass through every grey."""
    x0, y0, x1, y1 = region
    px = im.load()
    W, H = im.size
    near = lambda p: all(abs(p[i] - rgb[i]) <= slack for i in range(3))
    xs, ys = [], []
    for y in range(max(y0, solid), min(y1, H - solid)):
        for x in range(max(x0, solid), min(x1, W - solid)):
            if near(px[x, y]) and near(px[x - solid, y]) and near(px[x + solid, y]) and near(px[x, y - solid]) and near(px[x, y + solid]):
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
    meta = {"setup_on_real_windows": REAL_WINDOWS, "pictures": {}}
    src_dir = os.path.join(REPO, "docs", "shots", "wizard")
    for name, source, rings in SETUP:
        path = os.path.join(src_dir, source)
        im = Image.open(path).convert("RGB")
        w, h = im.size
        crop = (KEEP_FROM_X, 0, w, h)
        im.crop(crop).save(os.path.join(OUT, name), "JPEG", quality=84, optimize=True, progressive=True)
        entry = {"source": os.path.relpath(path, REPO), "source_sha256": sha(path), "says": SAYS[name], "rings": []}
        print("%s: %dx%d" % (name, crop[2] - crop[0], crop[3] - crop[1]))
        for r in rings:
            if r == "button":      # the primary button: bottom right
                b = box_of(im, GO, (w // 2, int(h * 0.8), w, h))
            else:                  # the chosen look's card
                b = box_of(im, SELECTED, (KEEP_FROM_X, int(h * 0.1), w, int(h * 0.5)))
            style = pct(b, crop, pad=-8 if r == "card" else 4)   # the card's ring sits inside its edge: outside it, it would cover the title
            entry["rings"].append({"for": r, "style": style})
            print("  ring %-6s %s" % (r, style))
        meta["pictures"][name] = entry
    src_dir = os.path.join(REPO, "docs", "shots", "shells")
    for name, source in LOOKS:
        path = os.path.join(src_dir, source)
        im = Image.open(path).convert("RGB")
        im.save(os.path.join(OUT, name), "JPEG", quality=82, optimize=True, progressive=True)
        meta["pictures"][name] = {"source": os.path.relpath(path, REPO), "source_sha256": sha(path), "says": SAYS[name], "size": list(im.size)}
        print("%s: %dx%d" % (name, im.size[0], im.size[1]))
    with open(os.path.join(OUT, "pictures.json"), "w") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    main()
