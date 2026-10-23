#!/usr/bin/env python3
"""fonts.py -- the installer's letters are the website's, as TrueType.

The website ships Atkinson Hyperlegible Next 400/700/800 as WOFF2
(website/assets/fonts, with the plain zero of tools/site/plainzero.py).
The installer embeds the same three faces as TrueType resources
(src/aurbridge/aurbridge-info.rc, AURFONT 100-102), because GDI cannot
read WOFF2. Run this after the website's fonts change; commit the result.

    python3 tools/wizard/fonts.py          # needs fontTools (and brotli)
"""
import os, shutil, sys
from fontTools.ttLib import TTFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SRC = os.path.join(ROOT, "website", "assets", "fonts")
DST = os.path.join(ROOT, "src", "aurbridge", "fonts")

for w in ("400", "700", "800"):
    f = TTFont(os.path.join(SRC, "atkinson-next-%s.woff2" % w))
    f.flavor = None
    out = os.path.join(DST, "atkinsonnext-%s.ttf" % w)
    f.save(out)
    print(out, os.path.getsize(out))
for t in ("OFL.txt", "FONTLOG.txt"):
    if os.path.exists(os.path.join(SRC, t)):
        shutil.copy(os.path.join(SRC, t), os.path.join(DST, t))
