#!/usr/bin/env python3
"""helptest: the help pages on the computer, checked the way she meets them.

    python3 -I tools/helptest.py                 check rootfs/usr/share/auros/help
    python3 -I tools/helptest.py --dir <folder>  check another copy

The Help button opens these pages (rootfs/usr/share/auros/help/) in the
browser, on a computer that may not be online yet -- "Getting online" is
one of them. Fails if:

  - a page the index promises is missing, or the index misses one;
  - any link, stylesheet or #place inside the folder does not exist;
  - anything would be fetched from elsewhere (http:, https:, //, file:),
    or a page has a script;
  - a topic page has no way back to the index or does not say how to
    close it ("Close this", the band's button);
  - visible text uses a word docs/EASY.md rule 2 forbids (the list is
    read from EASY.md itself, so the rule and the check cannot drift);
  - the stylesheet sets any text under 18px, or body text under 20px
    (docs/DESIGN-DIRECTION.md section 3, Body);
  - any text colour pair, light or dark, is under 4.5:1, or a border
    under 4.5:1 on light (3:1 on dark);
  - a picture of the bar is not exactly what tools/helppics.py draws from
    the bar's own geometry (the constants it uses must still be in foot.c);
  - a button the pages tell her to press (<span class="press">) is not a
    word the desktop's source says (src/aurshell, forge's program names)
    or one of the few other programs' buttons listed here: the name on the
    page is the name on the button;
  - a picture has no words for someone who cannot see it, sets its own
    style, or shows a button the bar at the bottom of the screen does not
    have (the labels are read from src/aurshell/foot.c), or does not mark
    exactly one of them;
  - the Help panel's button is not called what the index says it is
    (read from src/aurshell/foot.c), so "press Help, then ..." is true;
  - what "Put Windows back" removes is not said in the one pinned sentence
    in every place that says it (the Settings screen twice, the windows help
    page, the printable guide and the front page's questions);
  - a page spells out a web address: the pages work offline and are not
    rebuilt when the project moves, so they name no address at all.

Standard library only; no browser needed.
"""
import html
import html.parser
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helppics                    # noqa: E402  (draws the pictures; shares the bar's numbers)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

TOPICS = ["getting-online.html", "programs.html", "printing.html",
          "video-calls.html", "something-wrong.html", "windows.html",
          "never-call.html", "licenses.html"]

fails = []
passes = 0


def check(cond, what, why=""):
    global passes
    if cond:
        passes += 1
        print("ok   " + what)
    else:
        fails.append(what)
        print("FAIL " + what + (" -- " + why if why else ""))


class Page(html.parser.HTMLParser):
    """Links, ids, scripts and the words a person can see."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.refs, self.ids, self.text = [], set(), []
        self.scripts = 0
        self.skip = 0          # inside <script>/<style>/<title>
        self.svgs = []         # one dict per picture
        self.in_svg = self.in_text = 0
        self.presses, self.in_press = [], None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag in ("script", "style", "title"):
            self.skip += 1
        if tag == "script":
            self.scripts += 1
        for k in ("href", "src"):
            if a.get(k) is not None:
                self.refs.append(a[k])
        if a.get("id"):
            self.ids.add(a["id"])
        for k, v in attrs:
            if k.startswith("on"):
                self.scripts += 1          # an onclick= is a script too
        if tag == "span" and "press" in (a.get("class") or "").split():
            self.in_press = []
        if a.get("xlink:href") is not None:
            self.refs.append(a["xlink:href"])
        if tag == "svg":
            self.in_svg += 1
            self.svgs.append({"role": a.get("role"), "label": a.get("aria-label") or "",
                              "texts": [], "on": 0, "ring": 0, "styled": []})
        if self.in_svg:
            pic = self.svgs[-1]
            cls = (a.get("class") or "").split()
            pic["styled"] += [k for k in a if k in ("style", "font-size", "fill", "stroke")]
            if tag == "g" and "on" in cls:
                pic["on"] += 1
            if tag == "rect" and "ring" in cls:
                pic["ring"] += 1
            if tag == "text":
                self.in_text += 1

    def handle_endtag(self, tag):
        if tag in ("script", "style", "title") and self.skip:
            self.skip -= 1
        if tag == "span" and self.in_press is not None:
            self.presses.append(" ".join("".join(self.in_press).split()))
            self.in_press = None
        if tag == "svg" and self.in_svg:
            self.in_svg -= 1
        if tag == "text" and self.in_text:
            self.in_text -= 1

    def handle_data(self, data):
        if not self.skip:
            self.text.append(data)
        if self.in_press is not None:
            self.in_press.append(data)
        if self.in_text and data.strip():
            self.svgs[-1]["texts"].append(data.strip())


def band_labels():
    """The words on the bar's buttons, and the Help panel's one button,
    as src/aurshell/foot.c paints them."""
    try:
        with open(os.path.join(REPO, "src", "aurshell", "foot.c"), encoding="utf-8") as f:
            foot = f.read()
    except OSError:
        return set(), ""
    m = re.search(r"LABEL\[B_N\]\s*=\s*\{(.+?)\};", foot, re.S)
    labels = set(re.findall(r'"([^"]+)"', m.group(1))) if m else set()
    b = re.search(r'"(Show me how[^"]*)"', foot)
    return labels, (b.group(1) if b else "")


# Buttons in other people's programs that the pages name: Firefox's camera
# question, the print window, and Get more programs (GNOME Software).
OTHER_BUTTONS = {"Allow", "Block", "Print", "Install", "Open", "Installed", "Uninstall"}


def desktop_words():
    """Every string the desktop can show (src/aurshell), and the names
    build/forge gives programs: where a button's words can come from."""
    words = set()
    for root, _, files in os.walk(os.path.join(REPO, "src", "aurshell")):
        for f in files:
            if f.endswith(".c"):
                with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                    src = fh.read()
                    # A preview or test program (its own main) makes up
                    # sample text; the shell is main.c and the rest.
                    if f != "main.c" and re.search(r"^int main\(", src, re.M):
                        continue
                    # Comments quote button words too ("Let me look first"
                    # used to say...), so they are taken out first.
                    code = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
                    words |= set(re.findall(r'"((?:[^"\\\n]|\\.)*)"', code))
    try:
        with open(os.path.join(REPO, "build", "forge"), encoding="utf-8") as fh:
            words |= set(re.findall(r"^[\w.-]+\|([^|\n]+)\|", fh.read(), re.M))
    except OSError:
        pass
    return words


def forbidden_words():
    """docs/EASY.md rule 2's list: the italic run after "may not say"."""
    with open(os.path.join(REPO, "docs", "EASY.md"), encoding="utf-8") as f:
        easy = f.read()
    m = re.search(r"may not\s+say,[^*]*\*(.+?)\*", easy, re.S)
    if not m:
        return []
    words = [w.strip(" .\n") for w in m.group(1).replace("\n", " ").split(",")]
    return [w for w in words if w]


# ── colour ────────────────────────────────────────────────────────
def rgb(h):
    h = h.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


def lum(c):
    lin = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in rgb(c)]
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def ratio(a, b):
    x, y = lum(a), lum(b)
    return (max(x, y) + 0.05) / (min(x, y) + 0.05)


def tokens(block):
    return dict(re.findall(r"--([a-z0-9-]+)\s*:\s*(#[0-9A-Fa-f]{3,6})", block))


def main():
    args = sys.argv[1:]
    d = os.path.join(REPO, "rootfs", "usr", "share", "auros", "help")
    if "--dir" in args:
        d = os.path.abspath(args[args.index("--dir") + 1])
    print("checking", os.path.relpath(d, REPO) if d.startswith(REPO) else d)

    # 1. the pages
    pages = sorted(f for f in os.listdir(d) if f.endswith(".html")) if os.path.isdir(d) else []
    check("index.html" in pages, "index.html exists")
    for t in TOPICS:
        check(t in pages, f"{t} exists")
    check(os.path.isfile(os.path.join(d, "help.css")), "help.css exists")

    parsed = {}
    for p in pages:
        pg = Page()
        with open(os.path.join(d, p), encoding="utf-8") as f:
            pg.feed(f.read())
        parsed[p] = pg

    # 2. links inside the folder, and 3. nothing from elsewhere
    missing, outside = [], []
    for p, pg in parsed.items():
        for r in pg.refs:
            if re.match(r"^(https?:|//|file:|ftp:|data:)", r, re.I):
                outside.append(f"{r} on {p}")
                continue
            if re.match(r"^(mailto:|tel:|javascript:)", r, re.I):
                outside.append(f"{r} on {p}")
                continue
            target, _, frag = r.partition("#")
            target = target or p
            if "/" in target or target.startswith(".."):
                outside.append(f"{r} on {p} (leaves the folder)")
                continue
            if not os.path.isfile(os.path.join(d, target)):
                missing.append(f"{r} on {p}")
            elif frag and target.endswith(".html") and frag not in parsed.get(target, Page()).ids:
                missing.append(f"{r} on {p} (no such place)")
    check(not missing, f"every link and stylesheet exists ({sum(len(x.refs) for x in parsed.values())} checked)",
          "; ".join(missing[:8]))
    check(not outside, "nothing is fetched from anywhere else, and no link leaves the folder",
          "; ".join(outside[:8]))
    css_path = os.path.join(d, "help.css")
    css = open(css_path, encoding="utf-8").read() if os.path.isfile(css_path) else ""
    css_out = re.findall(r"url\(\s*['\"]?([^)'\"]+)", css) + re.findall(r"@import\s+['\"]?([^'\";]+)", css)
    check(not css_out, "the stylesheet loads nothing", ", ".join(css_out))
    scripted = [p for p, pg in parsed.items() if pg.scripts]
    check(not scripted, "no page has a script", ", ".join(scripted))

    # 4. the index lists every topic; every topic leads back and says how to close it
    idx = parsed.get("index.html")
    if idx:
        listed = {r.partition("#")[0] for r in idx.refs}
        check(all(t in listed for t in TOPICS), "the index links every topic",
              ", ".join(t for t in TOPICS if t not in listed))
    for t in TOPICS:
        pg = parsed.get(t)
        if not pg:
            continue
        check("index.html" in pg.refs, f"{t}: a way back to all the help")
        words = " ".join(pg.text)
        check("Close this" in words, f"{t}: says how to close it (Close this)")

    # 5. pictures: words for whoever cannot see them, the bar as it is
    labels, show_me = band_labels()
    check(len(labels) >= 6, f"read the bar's buttons from src/aurshell/foot.c ({len(labels)})")
    n_pics, bad = 0, []
    for p, pg in parsed.items():
        for i, pic in enumerate(pg.svgs, 1):
            n_pics += 1
            where = f"picture {i} on {p}"
            if pic["role"] != "img" or len(pic["label"].split()) < 4:
                bad.append(f"{where}: no role=img and aria-label")
            if pic["styled"]:
                bad.append(f"{where}: sets its own {', '.join(sorted(set(pic['styled'])))}")
            odd = [t for t in pic["texts"] if t not in labels]
            if odd:
                bad.append(f"{where}: the bar has no {', '.join(odd)}")
            if pic["on"] != 1 or pic["ring"] != 1:
                bad.append(f"{where}: marks {pic['on']} buttons and draws {pic['ring']} rings, not 1")
    check(n_pics >= 4, f"the pages have pictures of the bar ({n_pics})")
    check(not bad, "every picture is described in words, styled by help.css, and shows the real bar",
          "; ".join(bad[:8]))
    # the pictures are drawn, not hand-made: each is what helppics draws from foot.c's geometry
    try:
        with open(os.path.join(REPO, "src", "aurshell", "foot.c"), encoding="utf-8") as f:
            foot_src = f.read()
    except OSError:
        foot_src = ""
    lost = [g for g in helppics.GEOMETRY if g not in foot_src]
    check(not lost, "the bar's geometry is still what the pictures draw (tools/helppics.py)", "; ".join(lost))
    drawn_wrong = []
    for p in parsed:
        text = open(os.path.join(d, p), encoding="utf-8").read()
        for m in helppics.PIC.finditer(text):
            try:
                if helppics.bar(m.group(1), m.group(2)) != m.group(0):
                    drawn_wrong.append(f"{m.group(1)} {m.group(2)} on {p}")
            except SystemExit as e:
                drawn_wrong.append(str(e))
        if text.count("<svg") != len(helppics.PIC.findall(text)):
            drawn_wrong.append(f"{p}: a picture without data-bar")
    check(not drawn_wrong, "every picture is exactly what tools/helppics.py draws",
          "; ".join(drawn_wrong[:6]) + " (run python3 -I tools/helppics.py)")
    idx_text = html.unescape(" ".join(idx.text)) if idx else ""
    check(bool(show_me) and show_me in idx_text,
          f'the index names the Help panel\'s button as the bar paints it ("{show_me}")')

    # 5b. the name on the page is the name on the button
    known = desktop_words() | OTHER_BUTTONS
    check(len(known) > 200, f"read the desktop's words ({len(known)})")
    straight = lambda t: t.replace("\u2019", "'")          # the pages print curly quotes
    known = {straight(w) for w in known}
    unknown = sorted({f'"{w}" on {p}' for p, pg in parsed.items() for w in pg.presses
                      if straight(html.unescape(w)) not in known})
    n_press = sum(len(pg.presses) for pg in parsed.values())
    check(not unknown, f"every button the pages name is a button that exists ({n_press} checked)",
          "; ".join(unknown[:8]))

    # 5c. one truth about going back: the loss half. The words for each answer
    # agree everywhere; what "Put Windows back" REMOVES is said once, and every
    # place that says it carries it verbatim (the first letter may be a capital
    # or not: it begins a sentence in some places and not in others). Whoever
    # changes the sentence changes LOSS and every place together, or this is red.
    loss = re.compile(r"[Aa]nything you saved in AurOS goes with it\b")
    def loss_in(path):
        full = os.path.join(REPO, path)
        try:
            raw = open(full, encoding="utf-8").read()
        except OSError:
            return None
        if path.endswith(".c"):
            code = re.sub(r"/\*.*?\*/", " ", raw, flags=re.S)
            # in one string literal on one line: a sentence split across two
            # literals cannot be found by anyone who searches for it
            return len(re.findall(r'"[^"\n]*' + loss.pattern + r'[^"\n]*"', code))
        pg = Page(); pg.feed(raw)
        return len(loss.findall(" ".join(" ".join(pg.text).split())))
    wins = parsed.get("windows.html")
    n_win = len(loss.findall(" ".join(" ".join(wins.text).split()))) if wins else 0
    places = [("src/aurshell/settings.c", 2, "the Settings screen (its row and its page)"),
              ("website/guide.html", 1, "the printable guide"),
              ("website/index.html", 1, "the front page's questions")]
    lost = [] if n_win >= 1 else ["the windows help page"]
    for path, want, what in places:
        got = loss_in(path)
        if got is None or got < want:
            lost.append(f"{what} ({path}: {got} of {want})")
    check(not lost, "what Put Windows back removes is one sentence in every place: "
          '"Anything you saved in AurOS goes with it."', "missing in " + "; ".join(lost))

    # 6. no web address on any page
    addr = []
    for p, pg in parsed.items():
        text = html.unescape(" ".join(pg.text))
        for m in re.finditer(r"(https?://|www\.|github\.(com|io)|\b[\w-]+\.(com|org|net|io)\b)", text, re.I):
            addr.append(f"{m.group(0)} on {p}")
    check(not addr, "no page spells out a web address", "; ".join(addr[:8]))

    # 7. her words (docs/EASY.md rule 2)
    banned = forbidden_words()
    check(len(banned) >= 20, f"read docs/EASY.md's list of words she does not use ({len(banned)})")
    hits = []
    for p, pg in parsed.items():
        text = html.unescape(" ".join(pg.text + [x["label"] for x in pg.svgs]))
        for w in banned:
            if re.search(r"(?<![\w-])" + re.escape(w) + r"(?![\w-])", text, re.I):
                hits.append(f'"{w}" on {p}')
    check(not hits, "no word from docs/EASY.md rule 2 on any page", "; ".join(hits[:10]))

    # 8. sizes
    sizes = [(float(v), u) for v, u in re.findall(r"font-size\s*:\s*([\d.]+)(px|em|rem)", css)]
    sizes += [(float(v), "px") for v in re.findall(r"font\s*:[^;]*?\b([\d.]+)px", css)]
    small = [f"{v}{u}" for v, u in sizes if (u == "px" and v < 18) or (u != "px" and v < 0.75)]
    check(not small, "no text under 18px in the stylesheet", ", ".join(small))
    body = re.search(r"body\s*\{[^}]*font-size\s*:\s*([\d.]+)px", css)
    check(bool(body) and float(body.group(1)) >= 20, "body text 20px or larger",
          body.group(1) + "px" if body else "no body font-size")
    inline = [p for p, pg in parsed.items()
              if re.search(r"style\s*=", open(os.path.join(d, p), encoding="utf-8").read())]
    check(not inline, "no page overrides the stylesheet inline", ", ".join(inline))

    # 9. colour, light and dark
    light = tokens(css.split("@media")[0])
    dark_m = re.search(r"prefers-color-scheme:\s*dark\)\s*\{(.+?)\}\s*\}", css, re.S)
    schemes = [("light", light)]
    if dark_m:
        schemes.append(("dark", {**light, **tokens(dark_m.group(1))}))
    check(len(schemes) == 2, "a dark variant exists")
    # (text, background) pairs actually used by help.css
    pairs = [("ink", "bg"), ("ink-2", "bg"), ("ink", "soft"), ("ink-2", "soft"),
             ("ink", "card"), ("ink", "warn-bg"),
             ("bar-dim", "bar"), ("bar-ink", "bar-hot"), ("marker", "bar")]
    rules = [("rule", "bg"), ("rule", "card")]   # no frame is drawn on --soft
    for name, t in schemes:
        low = []
        for fg, bg in pairs + rules:
            if fg not in t or bg not in t:
                low.append(f"--{fg}/--{bg} undefined")
                continue
            r = ratio(t[fg], t[bg])
            floor = 4.5 if (fg, bg) in pairs or name == "light" else 3.0
            if r < floor:
                low.append(f"--{fg} on --{bg} {r:.2f}:1")
        check(not low, f"{name}: every text colour 4.5:1 or better, every border too"
              + ("" if name == "light" else " (3:1)"), "; ".join(low))

    print(f"\n{passes} passed, {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
