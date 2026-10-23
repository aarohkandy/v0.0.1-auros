#!/usr/bin/env python3
"""The website's letters: Atkinson Hyperlegible Next with a plain zero.

    python3 tools/site/plainzero.py UPSTREAM_DIR [OUT_DIR]

UPSTREAM_DIR holds the upstream static fonts (AtkinsonHyperlegibleNext-Regular,
-Bold and -ExtraBold .ttf, from fonts/ttf/ of
https://github.com/googlefonts/atkinson-hyperlegible-next, commit 7925f50,
SIL OFL 1.1). OUT_DIR is website/assets/fonts. Needs fontTools and brotli.

Why: the upstream zero carries a slash, and the font has no alternate zero
(its only substitution is the tabular zero, also slashed). "1Ø to 25 minutes"
is a number she has never seen written (docs/research/people-walkthrough-2026-10.md
A-03). So the zero is O's outline narrowed to the zero's width: a plain oval.
Everything else is the upstream font, subset to Latin-1, Latin Extended-A and
the punctuation the site uses. The stream that owns the installer's and the
desktop's copies can run the same function on its static TTFs (zero_plain()).

The script is repeatable: the same inputs give the same bytes' worth of font.
"""
import os
import sys

from fontTools import subset
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

UNICODES = "U+0020-007E,U+00A0-00FF,U+0100-017F,U+2013,U+2014,U+2018,U+2019,U+201C,U+201D,U+2022,U+2026"
FILES = [("Regular", "atkinson-next-400"), ("Bold", "atkinson-next-700"), ("ExtraBold", "atkinson-next-800")]


def zero_plain(font):
    """Redraw every zero glyph as the capital O's outline, narrowed to the
    zero's own advance width. Hinting for those glyphs is dropped (they are
    drawn without it)."""
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    glyf, hmtx = font["glyf"], font["hmtx"]
    o = cmap[ord("O")]
    o_advance = hmtx[o][0]
    changed = []
    for name in ("zero", "zero.tf"):
        if name not in glyf.keys():
            continue
        advance = hmtx[name][0]
        pen = TTGlyphPen(glyphs)
        glyphs[o].draw(TransformPen(pen, (advance / o_advance, 0, 0, 1, 0, 0)))
        glyph = pen.glyph()
        glyf[name] = glyph
        glyph.recalcBounds(glyf)
        hmtx[name] = (advance, glyph.xMin)
        changed.append(name)
    if not changed:
        sys.exit("plainzero: this font has no zero glyph")
    return changed


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "website", "assets", "fonts")
    for weight, name in FILES:
        font = TTFont(os.path.join(src, "AtkinsonHyperlegibleNext-%s.ttf" % weight))
        zero_plain(font)
        options = subset.Options()
        options.flavor = "woff2"
        options.layout_features = ["*"]
        options.hinting = True
        sub = subset.Subsetter(options)
        sub.populate(unicodes=subset.parse_unicodes(UNICODES))
        sub.subset(font)
        font.flavor = "woff2"
        target = os.path.join(out, name + ".woff2")
        font.save(target)
        print("%s: %d bytes" % (target, os.path.getsize(target)))


if __name__ == "__main__":
    main()
