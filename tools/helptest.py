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
    page, the printable guide and the install page's small print);
  - a page spells out a web address: the pages work offline and are not
    rebuilt when the project moves, so they name no address at all;
  - a question she may be stuck on (QUESTIONS below: the internet, her
    pictures and her Windows files, bigger letters, printing, music, a video
    call, a letter, email, a new program, turning it off, a frozen or dark
    screen, a forgotten PIN, a phone call or a page that says her computer is
    infected, going back to Windows, who to ask) has no page, a page that no
    longer says the words that answer it, or a page the index does not reach;
  - a page is longer than two screens (see SCREEN_PX), or does not point to
    "Who do I ask?";
  - a page says a thing about the desktop that the desktop's code no longer
    does (SAYS: the sentences it quotes; CODE_FACTS: how long the screen
    stays lit, how long "Close this" waits for its second press, what the
    power button and the lid do; Printers, which the desktop hides).

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

# What the index lists, one card each, in her words.
TOPICS = ["getting-online.html", "my-files.html", "everyday.html", "programs.html",
          "printing.html", "video-calls.html", "words-and-sound.html", "turn-off.html",
          "computer-full.html", "something-wrong.html", "scary-page.html", "never-call.html", "windows.html",
          "forgot-pin.html", "ask-someone.html", "licenses.html"]
# Pages the topics lead on to (windows.html links both). They lead back too.
SUBPAGES = ["remove-auros.html", "first-days.html"]
ASK = "ask-someone.html"

# "I'm stuck" must always have an answer: each question she may be stuck on, the
# page that answers it, and the words that make it an answer (the buttons she
# presses and the thing it is about). Whoever moves or empties one is red.
QUESTIONS = [
    ("how do I get on the internet",          "getting-online.html", ["Wi-Fi", "Connect", "cable", "Internet"]),
    ("how do I get on Wi-Fi after moving house", "getting-online.html", ["new home", "new internet box", "password"]),
    ("where are my pictures",                 "my-files.html", ["Photos", "Pictures", "Your files"]),
    ("where are my Windows files",            "my-files.html", ["still in Windows", "Bring my files across", "Not now"]),
    ("how do I make the letters bigger",      "words-and-sound.html", ["Bigger text", "Smaller text", "Ctrl"]),
    ("how do I make the sound louder",        "words-and-sound.html", ["How loud the sound is", "Settings"]),
    ("how do I print",                        "printing.html", ["Print", "printer"]),
    ("how do I listen to music or watch a video", "everyday.html", ["Music and video", "Your files"]),
    ("how do I video call my family",         "video-calls.html", ["camera and microphone", "Allow", "link"]),
    ("how do I write a letter",               "everyday.html", ["Letters", "Save"]),
    ("how do I use email",                    "everyday.html", ["Email", "Internet"]),
    ("how do I install something",            "programs.html", ["Get more programs", "Install", "Open"]),
    ("how do I turn it off",                  "turn-off.html", ["Turn off", "Turn it off", "Start it again"]),
    ("a message says the computer is getting full", "computer-full.html", ["getting full", "Downloads", "Delete", "Empty Trash"]),
    ("a program is stuck",                    "something-wrong.html", ["Close this", "Count to three"]),
    ("it is frozen",                          "something-wrong.html", ["Nothing on the screen answers", "power button", "Start it again"]),
    ("the screen went dark",                  "something-wrong.html", ["The screen went dark", "any key"]),
    ("I forgot my PIN",                       "forgot-pin.html", ["I forgot my PIN", "rescue code", "paper"]),
    ("someone phoned, saying Microsoft",      "never-call.html", ["Microsoft", "Hang up", "trick"]),
    ("someone phoned and I let them in",      "never-call.html", ["If you already let someone in", "Turn it off", "bank"]),
    ("a website says my computer is infected", "scary-page.html", ["virus", "Get me out of this page", "Close this page"]),
    ("how do I go back to Windows",           "windows.html", ["Put Windows back", "Start Windows this once"]),
    ("how do I remove AurOS",                 "remove-auros.html", ["Remove AurOS and put Windows back", "Save my files to a USB stick first"]),
    ("what the first question does",          "first-days.html", ["Yes, it all works", "No, go back to Windows", "Let me look first"]),
    ("who do I ask",                          ASK, ["someone you trust", "photo"]),
    ("my helper needs to see what went wrong", ASK, ["Save a report for whoever helps you", "USB stick"]),
]

# The sentences the pages quote, as the desktop says them: each must be on the
# page and be (part of) a string in the desktop's own code.
SAYS = [
    ("turn-off.html", "What should this computer do?"),
    ("never-call.html", "Before you install this"),
    ("forgot-pin.html", "Type your PIN to start AurOS"),
    ("forgot-pin.html", "six groups of four numbers"),
    ("forgot-pin.html", "Pick 4 to 6 numbers you will remember"),
    ("my-files.html", "Does everything work?"),
    ("first-days.html", "Does everything work?"),
]

# "A screen" is 750 px of page at 100% text: the panel of a 1366x768 laptop less
# the bar at the bottom, with room to spare. (At the smallest panel, 1024x600,
# it is about 450 px, so these pages are three screens there. Sizes are modelled
# from the stylesheet below; measured in a browser at 1024 px wide, the model
# is within 6%, and no page measures over 1430 px.)
SCREEN_PX = 750
MAX_SCREENS = 2

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
# (and "Add", the Printers window's own button, and "Trash" and "Empty Trash", in her files.)
OTHER_BUTTONS = {"Allow", "Block", "Print", "Install", "Open", "Installed", "Add", "Trash", "Empty Trash"}


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
                    # (lockpin.c has a main of its own, and is also the PIN
                    # page she sees at the start: its words are real.)
                    if f not in ("main.c", "lockpin.c") and re.search(r"^int main\(", src, re.M):
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


class Tall(html.parser.HTMLParser):
    """How tall a page is at 100% text and 1024 px wide, from help.css's own
    numbers (a 32 px line, 750 px of text, margins as the stylesheet has them).
    A model, not a browser: it wraps by counting letters, and a browser measured
    it within 6% on every page here. Enough to say "a wall of words" and "not"."""

    # letters to a line: h1, h2, a card of the index, a numbered step, a list
    # item or a paragraph, the lede, a paragraph in a box
    CPL = dict(h1=38, h2=59, card=27, ol=74, p=86, lede=81, box=81)

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.h = 66 + 8 + 48          # the bar, the top of main, the foot of main
        self.buf, self.cur, self.ctx, self.svg, self.cards = None, None, [], 0, []

    def lines(self, text, cpl):
        return max(1, -(-len(" ".join("".join(text).split())) // cpl))

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        cls = (a.get("class") or "").split()
        if tag == "svg":
            self.svg += 1
            self.h += 144               # a picture of the bar and its margins
            return
        if self.svg:
            return
        if tag in ("ol", "ul", "dl", "div"):
            self.ctx.append((tag, cls))
        topics = any(c[0] == "ul" and "topics" in c[1] for c in self.ctx)
        if tag in ("h1", "h2", "p", "li", "dt", "dd") or (tag == "b" and topics):
            self.buf, self.cur = [], (tag, cls, list(self.ctx))
        if tag == "span" and "press" in cls and self.buf is not None:
            self.buf.append("xx")       # a pressable word has a frame round it

    def handle_endtag(self, tag):
        if tag == "svg":
            self.svg -= 1
            return
        if self.svg:
            return
        if tag in ("ol", "ul", "dl", "div") and self.ctx:
            t, c = self.ctx.pop()
            if t == "ul" and "topics" in c:       # two columns of cards
                rows = [self.cards[i:i + 2] for i in range(0, len(self.cards), 2)]
                self.h += 48 + sum(max(r) + 12 for r in rows)
            else:
                self.h += {"ol": 24, "ul": 16, "dl": 24, "div": 24 + (32 if "warn" in c else 0)}[t]
        if self.buf is None or self.cur is None or self.cur[0] != tag:
            return
        t, cls, ctx = self.cur
        inside = lambda x: any(c[0] == x for c in ctx)
        C = self.CPL
        if t == "b":
            self.cards.append(self.lines(self.buf, C["card"]) * 34 + 36)
        elif t == "li" and any("topics" in c[1] for c in ctx):
            pass
        elif t == "h1":
            self.h += self.lines(self.buf, C["h1"]) * 48 + 44
        elif t == "h2":
            self.h += self.lines(self.buf, C["h2"]) * 34 + 56
        elif t == "li" and inside("ol"):
            self.h += self.lines(self.buf, C["ol"]) * 32 + 20
        elif t == "li":
            self.h += self.lines(self.buf, C["p"]) * 32 + 8
        elif t in ("dt", "dd"):
            self.h += self.lines(self.buf, C["p"]) * 32 + (16 if t == "dt" else 8)
        elif "lede" in cls:
            self.h += self.lines(self.buf, C["lede"]) * 32 + 16
        elif "done" in cls:
            self.h += 74 + self.lines(self.buf, C["p"]) * 32 + 16
        else:
            self.h += self.lines(self.buf, C["box"] if inside("div") else C["p"]) * 32 + 16
        self.buf, self.cur = None, None

    def handle_data(self, data):
        if self.buf is not None and not self.svg:
            self.buf.append(data)


def page_height(text):
    t = Tall()
    t.feed(text)
    return t.h


def read(path):
    try:
        with open(os.path.join(REPO, path), encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def code_facts(flat):
    """What the pages promise about how the desktop behaves, read from the code
    that does it, so that changing the code (or the page) turns this red."""
    main_c = read("src/aurshell/main.c")
    wait = re.search(r"#define\s+CLOSE_INSIST_MS\s+(\d+)", main_c)
    check(bool(wait) and int(wait.group(1)) >= 4000 and "aurwl_win_kill(win)" in main_c,
          'a second press of Close this stops a program that does not answer, and waits 4 s or more for it (the page says "Count to three")',
          f"CLOSE_INSIST_MS is {wait.group(1) if wait else 'gone'}")
    dark = re.search(r'theme_int\(&p,\s*"screen_off_minutes",\s*(\d+)\)', main_c)
    check(bool(dark) and dark.group(1) == "10" and "ten minutes" in flat.get("something-wrong.html", ""),
          'the screen goes dark after ten minutes, as the stuck page says', f"the desktop says {dark.group(1) if dark else '?'}")
    check("woke_at_ms = tnow;" in main_c and "screen_dark = 0;" in main_c,
          "the press that wakes a dark screen does nothing else (the stuck page says so)")
    logind = read("rootfs/etc/systemd/logind.conf.d/50-auros.conf")
    check("HandlePowerKey=poweroff" in logind and "HandleLidSwitch=suspend" in logind,
          "the power button turns the computer off and closing the lid puts it to sleep (the turn-off page says so)")
    check(re.search(r"keeps its size until it is\s+reopened", read("src/aurshell/textsys.h")) is not None,
          "a program that was already open keeps its words until it is opened again (the bigger words page says so)")
    # Ubuntu's Printers entry is in the Settings category X-GNOME-Settings-Panel, which the
    # desktop leaves out of her programs (apps.c is_settings_page): the way to it is the
    # Settings row, which starts the tool (settings.c R_PRINTERS), and the page says so.
    settings_c = read("src/aurshell/settings.c")
    check('case R_PRINTERS: return "Printers";' in settings_c and "c->spawn(c, blob, 1)" in settings_c,
          "Settings has a Printers row that starts the printer tool (the printing page sends her there)")
    printing = flat.get("printing.html", "")
    check(re.search(r"Press Settings at the bottom of the screen, then Printers", printing) is not None,
          "the printing page says how to reach it: Settings, then Printers")
    check('case R_FILES:   return "Bring my files across";' in settings_c and
          re.search(r"press Settings at the bottom of the screen, then Bring my files across", flat.get("my-files.html", "")) is not None,
          "the files page says where \"Not now\" leaves them: Settings, then Bring my files across (a Settings row)")
    # A program that has frozen never answers "Close this". The first press says on the screen
    # that a second one stops it (main.c), as the help page says.
    check("If it asks you something, answer it." in read("src/aurshell/main.c") and
          "press Close this again." in read("src/aurshell/main.c") and
          re.search(r"press Close this again", flat.get("something-wrong.html", ""), re.I) is not None,
          "the first press of Close this says on the screen to press it again (the stuck page says so too)")
    hidden = 'has_cat(e, "X-GNOME-Settings-Panel")' in read("src/aurshell/apps.c")
    check(not (hidden and re.search(r"\bPrinters\b", printing) and "R_PRINTERS" not in settings_c),
          "the printing page does not send her to a Printers program that the desktop does not list")

def main():
    args = sys.argv[1:]
    d = os.path.join(REPO, "rootfs", "usr", "share", "auros", "help")
    if "--dir" in args:
        d = os.path.abspath(args[args.index("--dir") + 1])
    print("checking", os.path.relpath(d, REPO) if d.startswith(REPO) else d)

    # 1. the pages
    pages = sorted(f for f in os.listdir(d) if f.endswith(".html")) if os.path.isdir(d) else []
    check("index.html" in pages, "index.html exists")
    for t in TOPICS + SUBPAGES:
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
    wins_pg = parsed.get("windows.html")
    reached = {r.partition("#")[0] for r in wins_pg.refs} if wins_pg else set()
    check(all(t in reached for t in SUBPAGES), "windows.html leads on to every page under it",
          ", ".join(t for t in SUBPAGES if t not in reached))
    for t in TOPICS + SUBPAGES:
        pg = parsed.get(t)
        if not pg:
            continue
        check("index.html" in pg.refs, f"{t}: a way back to all the help")
        words = " ".join(pg.text)
        check("Close this" in words, f"{t}: says how to close it (Close this)")
        # She may be stuck on any page, and the answer to that is always one press away.
        if t != ASK:
            check(ASK in {r.partition("#")[0] for r in pg.refs}, f"{t}: points to Who do I ask?")

    # 4b. every question she may be stuck on has a page, reached from the index
    flat = {p: " ".join(html.unescape(" ".join(pg.text)).split()) for p, pg in parsed.items()}
    linked = {r.partition("#")[0] for r in (idx.refs if idx else [])} | set(SUBPAGES)
    gaps = []
    for q, page, words in QUESTIONS:
        if page not in flat:
            gaps.append(f"{q}: no {page}")
            continue
        if page not in linked:
            gaps.append(f"{q}: the index does not reach {page}")
        miss = [w for w in words if w not in flat[page]]
        if miss:
            gaps.append(f"{q}: {page} no longer says {', '.join(repr(m) for m in miss)}")
    check(not gaps, f"every question she may be stuck on has a page that answers it ({len(QUESTIONS)})",
          "; ".join(gaps[:6]))

    # 4c. no page is longer than two screens
    css_text = open(os.path.join(d, "help.css"), encoding="utf-8").read() if os.path.isfile(os.path.join(d, "help.css")) else ""
    long_ = []
    for p in pages:
        px = page_height(open(os.path.join(d, p), encoding="utf-8").read())
        if px > MAX_SCREENS * SCREEN_PX:
            long_.append(f"{p} is about {px} px, {px / SCREEN_PX:.1f} screens")
    check(not long_, f"no page is longer than {MAX_SCREENS} screens ({MAX_SCREENS * SCREEN_PX} px at 100% text)",
          "; ".join(long_))

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
    said = []
    for page, phrase in SAYS:
        if phrase not in flat.get(page, ""):
            said.append(f'{page} does not say "{phrase}"')
        elif not any(phrase in straight(w) for w in known):
            said.append(f'"{phrase}" on {page} is not on any screen of the desktop')
    check(not said, f"the screens the pages quote say what the pages say ({len(SAYS)} checked)", "; ".join(said))
    code_facts(flat)

    # 5c. one truth about going back: what happens to her files. The words for
    # each answer agree everywhere; what "Put Windows back" does with what she
    # saved is said once (both kinds of install copy her six folders into
    # Windows first, answer.sh back_files), and every place that says it
    # carries it verbatim (the first letter may be a capital or not: it begins
    # a sentence in some places and not in others). Whoever changes the sentence
    # changes LOSS and every place together, or this is red.
    loss = re.compile(r"[Yy]our documents, pictures, music and videos are copied into Windows first\b")
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
    places = [("src/aurshell/settings.c", 1, "the Settings screen (its page; its row is too short to hold it, windowspagetest checks that)"),
              ("website/guide.html", 1, "the printable guide"),
              ("website/install.html", 1, "the install page's small print")]
    rem = parsed.get("remove-auros.html")
    n_rem = len(loss.findall(" ".join(" ".join(rem.text).split()))) if rem else 0
    lost = [] if n_win >= 1 else ["the windows help page"]
    if n_rem < 1:
        lost.append("the remove AurOS help page")
    for path, want, what in places:
        got = loss_in(path)
        if got is None or got < want:
            lost.append(f"{what} ({path}: {got} of {want})")
    check(not lost, "what Put Windows back does with her files is one sentence in every place: "
          '"Your documents, pictures, music and videos are copied into Windows first."', "missing in " + "; ".join(lost))
    # ...and the old one, which was true of no install since the way back saved her files,
    # does not creep back in anywhere she reads
    old = re.compile(r"[Aa]nything you saved in AurOS goes with it\b")
    stale = []
    for path in ["src/aurshell/settings.c", "website/guide.html", "website/install.html", "website/index.html"]:
        try:
            if old.search(open(os.path.join(REPO, path), encoding="utf-8").read()):
                stale.append(path)
        except OSError:
            pass
    for pg_name in ("windows.html", "remove-auros.html"):
        pg_ = parsed.get(pg_name)
        if pg_ and old.search(" ".join(" ".join(pg_.text).split())):
            stale.append("the " + pg_name + " help page")
    check(not stale, "no page says her files go with AurOS (they are copied into Windows first)", "still in " + "; ".join(stale))

    # 6. no web address on any page
    addr = []
    for p, pg in parsed.items():
        # An address to write to is not a web address (the source offer
        # on licenses.html must name one: tools/legal/check.py set-contact).
        text = re.sub(r"[\w.+-]+@[\w-]+(\.[\w-]+)+", " ", html.unescape(" ".join(pg.text)))
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
    rules = [("rule", "bg"), ("rule", "card"), ("rule", "soft")]   # .bar border sits on --soft
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
