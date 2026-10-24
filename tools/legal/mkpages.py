#!/usr/bin/env python3
"""mkpages.py -- the legal pages of the website, made from the texts in docs/legal/.

    python3 -I tools/legal/mkpages.py           write website/{terms,disclaimer,privacy,licenses}.html
                                                and the legal footer of every page that has a footer
    python3 -I tools/legal/mkpages.py --check   exit 1 if any of them is not what this would write

docs/legal/TERMS.md, DISCLAIMER.md, PRIVACY.md and LICENSES.md are the words; the pages are
only their shape, so the website and the files in the source code cannot say different things.
The Markdown is a small subset: # and ## headings, paragraphs, - lists, tables (shown as lists,
which read better on a phone), **bold**, `code` and [text](address). RELEASES_URL becomes the
repository's releases address, filled in by tools/site/stamp.sh like every other address.

The footer block between <!-- legal-footer --> and <!-- /legal-footer --> is the same on every
page (index, guide, advanced, status, code-signing and the four new ones): the links to
the four pages, what the site collects, the license of AurOS's code, and the non-affiliation
paragraph (docs/legal/COMPLIANCE.md TM-3, LIC-2, PRI-1).
"""
import html, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SITE = os.path.join(ROOT, "website")

PAGES = [  # (markdown, page, title, description, show contents)
    ("docs/legal/TERMS.md", "terms.html", "Terms",
     "The terms of using AurOS, in plain words, written without a lawyer.", True),
    ("docs/legal/DISCLAIMER.md", "disclaimer.html", "Disclaimer",
     "AurOS is free and comes as it is. What we have tested, what we haven't, and why to copy your photos first.", False),
    ("docs/legal/PRIVACY.md", "privacy.html", "Privacy",
     "What AurOS collects (nothing), and which other organizations' servers your computer talks to.", True),
    ("docs/legal/LICENSES.md", "licenses.html", "Licenses and source code",
     "The licenses of the programs inside AurOS, and how to get their source code.", False),
]

_TM = open(os.path.join(ROOT, "docs/legal/TRADEMARKS.txt"), encoding="utf-8").read().strip().split("\n")
UBUNTU, TM = _TM[0], _TM[1]    # docs/legal/TRADEMARKS.txt is the one place these are written


def smart(t):
    t = re.sub(r"(\w)'(\w)", "\\1\u2019\\2", t)
    t = re.sub(r"'(\w)", "\u2018\\1", t) if False else t
    t = t.replace("'", "\u2019")
    return re.sub(r'"([^"]+)"', "\u201c\\1\u201d", t)


def inline(t):
    parts = re.split(r"(`[^`]+`)", t)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 2:
            out.append('<span class="words">%s</span>' % html.escape(p[1:-1], quote=False))
            continue
        p = html.escape(smart(p), quote=False)
        p = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", p)
        p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: '<a href="%s">%s</a>' % (
            "https://github.com/@AUROS_REPO@/releases" if m.group(2) == "RELEASES_URL" else m.group(2), m.group(1)), p)
        out.append(p)
    return "".join(out)


def blocks(md):
    """-> list of (kind, payload): h1, h2, p, ul (items), table (rows)"""
    out, para, lst, tab = [], [], [], []
    def flush():
        nonlocal para, lst, tab
        if para: out.append(("p", " ".join(para))); para = []
        if lst: out.append(("ul", lst)); lst = []
        if tab: out.append(("table", tab)); tab = []
    for line in md.split("\n"):
        if line.startswith("# "): flush(); out.append(("h1", line[2:]))
        elif line.startswith("## "): flush(); out.append(("h2", line[3:]))
        elif line.startswith("- "): 
            if para or tab: flush()
            lst.append(line[2:])
        elif line.startswith("|"):
            if para or lst: flush()
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if not all(re.fullmatch(r"-+", c) for c in cells): tab.append(cells)
        elif line.strip() == "": flush()
        else:
            if lst and line.startswith("  "): lst[-1] += " " + line.strip()
            else:
                if lst or tab: flush()
                para.append(line.strip())
    flush()
    return out


def render_block(kind, payload):
    if kind == "p":
        return "<p>%s</p>" % inline(payload)
    if kind == "ul":
        return "<ul>\n%s\n</ul>" % "\n".join("  <li><p>%s</p></li>" % inline(i) for i in payload)
    if kind == "table":
        head, rows = payload[0], payload[1:]
        lis = []
        for r in rows:
            lis.append("  <li><p><b>%s</b>%s</p></li>" % (inline(r[0]), "".join(". " + inline(c) if c else "" for c in r[1:]) if len(r) > 1 else ""))
        return "<ul>\n%s\n</ul>" % "\n".join(lis)
    raise ValueError(kind)


def slug(t):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", t.lower())).strip("-")


FOOTER = """    <!-- legal-footer: written by tools/legal/mkpages.py, the same on every page -->
    <ul class="foot-links foot-legal">
      <li><a href="licenses.html">Licenses and source code</a></li>
      <li><a href="terms.html">Terms</a></li>
      <li><a href="disclaimer.html">Disclaimer</a></li>
      <li><a href="privacy.html">Privacy</a></li>
    </ul>
    <p>No cookies and no ads. This site collects nothing about you. GitHub, which hosts it, keeps its own records of the internet addresses that visit. Write to us: CONTACT_EMAIL.</p>
    <p>AurOS&rsquo;s own code is free software (MIT license). It includes programs by others, each under its own license: <a href="licenses.html">licenses and source code</a>. Set in Atkinson Hyperlegible Next (<a href="assets/fonts/OFL.txt">license</a>).</p>
    <p>%s %s</p>
    <!-- /legal-footer -->""" % (html.escape(UBUNTU, quote=False), html.escape(TM, quote=False))

SHELL = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s &mdash; AurOS</title>
<meta name="description" content="%(desc)s">
<meta name="color-scheme" content="light">
<meta name="theme-color" content="#F6F2E9">
<link rel="icon" href="data:image/svg+xml,%%3Csvg%%20xmlns='http://www.w3.org/2000/svg'%%20viewBox='0%%200%%2032%%2032'%%3E%%3Cpath%%20d='M16%%202L30%%2016L16%%2030L2%%2016Z'%%20fill='%%230B5D4F'/%%3E%%3C/svg%%3E">
<link rel="stylesheet" href="assets/site.css">
</head>
<body>
<a class="skip" href="#main">Skip to the text</a>

<header class="bar">
  <div class="wrap bar-in">
    <a class="brand" href="index.html"><svg class="mark" viewBox="0 0 10 10" aria-hidden="true" focusable="false"><path d="M5 0L10 5L5 10L0 5Z"/></svg>AurOS</a>
    <a class="bar-link" href="index.html">Back to the simple page</a>
  </div>
</header>

<main id="main">

  <section class="page-head" aria-labelledby="page-title">
    <div class="narrow">
      <h1 id="page-title">%(h1)s</h1>
%(intro)s%(toc)s    </div>
  </section>
%(sections)s</main>

<footer class="foot">
  <div class="wrap">
    <ul class="foot-links">
      <li><a href="index.html">The simple page</a></li>
      <li><a href="advanced.html">For someone who knows computers</a></li>
      <li><a href="https://github.com/@AUROS_REPO@/issues">Ask a question</a></li>
      <li><a href="https://github.com/@AUROS_REPO@">The source code</a></li>
    </ul>
%(footer)s
  </div>
</footer>
</body>
</html>
"""


def page(md_path, title, desc, toc):
    with open(os.path.join(ROOT, md_path), encoding="utf-8") as fh:
        bl = blocks(fh.read())
    assert bl[0][0] == "h1", md_path
    h1 = bl[0][1]
    i = 1
    intro_blocks = []
    while i < len(bl) and bl[i][0] != "h2":
        intro_blocks.append(bl[i]); i += 1
    # the first line of every text is its version and who wrote it: small, then the lede
    intro = []
    for n, (k, p) in enumerate(intro_blocks):
        cls = ' class="note"' if n == 0 else (' class="lede"' if n == 1 and k == "p" else "")
        intro.append("      " + render_block(k, p).replace("<p>", "<p%s>" % cls, 1) if k == "p" else "      " + render_block(k, p))
    secs, heads = [], []
    n = 0
    while i < len(bl):
        assert bl[i][0] == "h2", (md_path, bl[i])
        name = bl[i][1]; i += 1
        body = []
        while i < len(bl) and bl[i][0] != "h2":
            body.append(render_block(*bl[i])); i += 1
        sid = slug(name); heads.append((sid, name))
        secs.append('\n  <section class="doc%s" id="%s" aria-labelledby="%s-title">\n    <div class="narrow">\n'
                    '      <h2 id="%s-title">%s</h2>\n%s\n      <p class="totop"><a href="#main">Back to the top</a></p>\n    </div>\n  </section>\n'
                    % (" sand" if len(secs) % 2 == 0 else "", sid, sid, sid, inline(name), "\n".join("      " + b.replace("\n", "\n      ") for b in body)))
    tochtml = ""
    if toc:
        tochtml = '      <ul class="toc" aria-label="On this page">\n%s\n      </ul>\n' % "\n".join(
            '        <li><a href="#%s">%s</a></li>' % (s, inline(h)) for s, h in heads)
    return SHELL % {"title": html.escape(title, quote=False), "desc": html.escape(desc), "h1": inline(h1),
                    "intro": ("\n".join(intro) + "\n") if intro else "", "toc": tochtml,
                    "sections": "".join(secs), "footer": FOOTER}


FOOT_RE = re.compile(r"(<footer class=\"foot\">\s*<div class=\"wrap\">\s*<ul class=\"foot-links\">.*?</ul>\n)(.*?)(  </div>\n</footer>)", re.S)


def with_footer(text):
    m = FOOT_RE.search(text)
    if not m:
        return None
    return text[:m.start(2)] + FOOTER + "\n" + text[m.end(2):]


def outputs():
    out = {}
    for md, name, title, desc, toc in PAGES:
        out["website/" + name] = page(md, title, desc, toc)
    for name in ("index", "guide", "advanced", "status", "code-signing", "request"):
        rel = "website/%s.html" % name
        with open(os.path.join(ROOT, rel), encoding="utf-8") as fh:
            t = fh.read()
        n = with_footer(t)
        if n is None:
            raise SystemExit("mkpages: %s has no footer in the expected shape" % rel)
        out[rel] = n
    return out


def main(argv):
    bad = 0
    for rel, text in outputs().items():
        p = os.path.join(ROOT, rel)
        have = open(p, encoding="utf-8").read() if os.path.isfile(p) else None
        if "--check" in argv:
            if have != text:
                print("mkpages: %s is not what tools/legal/mkpages.py writes (run it)" % rel); bad += 1
        elif have != text:
            open(p, "w", encoding="utf-8").write(text)
            print("mkpages: wrote " + rel)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
