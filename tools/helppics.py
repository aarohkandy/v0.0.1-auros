#!/usr/bin/env python3
"""helppics: the pictures of the bar at the bottom of the screen, in the help
pages, drawn from the bar's own code.

    python3 -I tools/helppics.py          redraw every picture in the help pages
    python3 -I tools/helppics.py --check  say which pictures differ (tools/helptest.py does this)

A picture is an inline <svg> whose data-bar says what to draw: the corner,
and the button to mark, e.g. data-bar="left Wi-Fi". It is drawn at the
bar's own size at 100% text (src/aurshell/foot.c): the same buttons in the
same order, each an outlined box with its icon above its label, widths from
item_w(), gaps FOOT_GAP and FOOT_BIG_GAP, in the bar's fixed colours (help.css
--bar*). The left corner shows Help, Wi-Fi and Settings, and Close this when
it is the one marked (it is on the bar only while a program is open).
Icons mirror shellcommon.c's shell_icon_draw() on its 24-unit grid.

When the bar changes, helptest goes red; run this, look at the pages, commit.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
HELP = os.path.join(REPO, "rootfs", "usr", "share", "auros", "help")

# foot.c, at 100% text: item_w() = max(96, 10.4*len(label) + 28), FOOT_GAP 8,
# FOOT_BIG_GAP 24 (between Smaller text and Bigger text), FOOT_PAD 6,
# item_h() = 38 + 22 with the icon above the label. helptest checks these
# numbers are still the ones in foot.c.
GEOMETRY = ["#define FOOT_GAP 8", "#define FOOT_BIG_GAP 24", "#define FOOT_PAD     6",
            "10.4f * k * (float)strlen(LABEL[which])) + (int)(28.f * k)",
            "(int)(38.f + 22.f * k)"]
LEFT = ["Help", "Wi-Fi", "Settings", "Close this"]
RIGHT = ["Smaller text", "Bigger text", "Turn off"]       # left to right
W, PAD, GAP, BIG_GAP, BH = 640, 6, 8, 24, 60
BAND = 72                       # BH + 2 * PAD
TOP = 40                        # some of the screen above it
H = TOP + BAND


def item_w(label):
    return max(96, int(10.4 * len(label)) + 28)


def icon(name, cx, cy):
    """The bar's icons (shellcommon.c), 22 px on a 24-unit grid. Strokes take
    their colour from help.css (.i ink line, .f ink fill, .p the bar's own
    colour, for what is cut out)."""
    def arc(r, a0, a1, oy, big=0):
        p = lambda a: (round(r * math.cos(math.radians(a)), 2), round(oy + r * math.sin(math.radians(a)), 2))
        (x0, y0), (x1, y1) = p(a0), p(a1)
        return f"M{x0} {y0}A{r} {r} 0 {big} 1 {x1} {y1}"
    g = []
    if name == "Help":
        g = ['<circle class="f" r="11"/>',
             f'<path class="q" stroke-width="2.7" d="{arc(4.3, -195, 75, -3.2, 1)}M2.2 .6L0 2.9V4.4"/>',
             '<circle class="p" cx="0" cy="7.6" r="1.7"/>']
    elif name == "Wi-Fi":
        g = [f'<path class="i" stroke-width="2.6" d="{arc(r, -135, -45, 7)}"/>' for r in (5, 9, 13)]
        g.append('<circle class="f" cy="7" r="2.1"/>')
    elif name == "Settings":
        teeth = "".join(f"M{round(6 * math.cos(k * .785398), 2)} {round(6 * math.sin(k * .785398), 2)}"
                        f"L{round(10.2 * math.cos(k * .785398), 2)} {round(10.2 * math.sin(k * .785398), 2)}"
                        for k in range(8))
        g = [f'<path class="i" stroke-width="4.4" stroke-linecap="butt" d="{teeth}"/>',
             '<circle class="f" r="7.6"/>', '<circle class="p" r="3.1"/>']
    elif name == "Close this":
        g = ['<rect class="i" x="-9" y="-9" width="18" height="18" stroke-width="2"/>',
             '<path class="i" stroke-width="2.4" d="M-4.6 -4.6L4.6 4.6M4.6 -4.6L-4.6 4.6"/>']
    elif name in ("Smaller text", "Bigger text"):
        g = ['<path class="i" stroke-width="2.8" stroke-linecap="butt" d="M-4.4 -9.5L-10.4 10M-4.4 -9.5L1.6 10"/>',
             '<rect class="f" x="-8.6" y="3.2" width="8.4" height="2.4"/>',
             '<rect class="f" x="5" y="-5.2" width="7" height="2.4"/>']
        if name == "Bigger text":
            g.append('<rect class="f" x="7.3" y="-7.5" width="2.4" height="7"/>')
    elif name == "Turn off":
        g = [f'<path class="i" stroke-width="2.8" d="{arc(8, -60, 240, 1.2, 1)}M0 -10.5V-.5"/>']
    s = 22 / 24
    return f'<g transform="translate({cx} {cy}) scale({s:.4f})">' + "".join(g) + "</g>"


def bar(side, marked):
    """The SVG for one corner of the bar."""
    labels = LEFT if marked == "Close this" else LEFT[:3]
    if side == "right":
        labels = RIGHT
    if marked not in labels:
        raise SystemExit(f"helppics: {marked!r} is not on the {side} of the bar ({labels})")
    xs, x = [], PAD
    if side == "left":
        for lab in labels:
            xs.append(x)
            x += item_w(lab) + GAP
    else:                                               # laid out from the right corner inwards
        x = W - PAD
        for lab in reversed(labels):
            x -= item_w(lab)
            xs.append(x)
            x -= BIG_GAP if lab == "Bigger text" else GAP
        xs.reverse()
    y0 = TOP + PAD
    if side == "left":
        shape = f"M0 0H{W}V{H}H20Q0 {H} 0 {H - 20}Z"
        band = f"M0 {TOP}H{W}V{H}H20Q0 {H} 0 {H - 20}Z"
    else:
        shape = f"M0 0H{W}V{H - 20}Q{W} {H} {W - 20} {H}H0Z"
        band = f"M0 {TOP}H{W}V{H - 20}Q{W} {H} {W - 20} {H}H0Z"
    names = ", ".join(labels[:-1]) + " and " + labels[-1]
    corner = "bottom left" if side == "left" else "bottom right"
    out = [f'<svg class="pic" data-bar="{side} {marked}" viewBox="0 0 {W} {H}" role="img" '
           f'aria-label="The {corner} corner of the screen. On the bar: {names}. {marked} is marked.">',
           f'<path class="scr" d="{shape}"/><path class="band" d="{band}"/>']
    for lab, x in zip(labels, xs):
        w = item_w(lab)
        on = ' class="on"' if lab == marked else ""
        out.append(f'<g{on}><rect class="btn" x="{x}" y="{y0}" width="{w}" height="{BH}" rx="8"/>'
                   f'{icon(lab, x + w / 2, y0 + 17)}'
                   f'<text x="{x + w / 2}" y="{y0 + BH - 12}">{lab}</text></g>')
    x = xs[labels.index(marked)]
    w = item_w(marked)
    out.append(f'<rect class="ring" x="{x - 2}" y="{y0 - 2}" width="{w + 4}" height="{BH + 4}" rx="10"/>')
    out.append("</svg>")
    return "\n".join(out)


PIC = re.compile(r'<svg class="pic" data-bar="(left|right) ([^"]+)".*?</svg>', re.S)


def redraw(text):
    return PIC.sub(lambda m: bar(m.group(1), m.group(2)), text)


def main():
    check = "--check" in sys.argv
    stale = []
    for name in sorted(os.listdir(HELP)):
        if not name.endswith(".html"):
            continue
        p = os.path.join(HELP, name)
        old = open(p, encoding="utf-8").read()
        new = redraw(old)
        if new != old:
            stale.append(name)
            if not check:
                open(p, "w", encoding="utf-8").write(new)
    if check:
        print("pictures out of date: " + ", ".join(stale) if stale else "every picture is as drawn")
    elif stale:
        print("redrawn: " + ", ".join(stale))
    return 1 if (check and stale) else 0


if __name__ == "__main__":
    sys.exit(main())
