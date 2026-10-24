#!/bin/sh
# wordlint: the words she reads, checked against the voice rules.
#
#   sh tools/wordlint.sh            every surface below; exit 1 on any hit
#   sh tools/wordlint.sh FILE...    only these files
#
# docs/DESIGN-DIRECTION.md section 5 K P1-6 and section 4, docs/EASY.md
# rule 2. Fails when anything she can read says:
#   - a word EASY.md rule 2 forbids (the list is read from EASY.md, so the
#     rule and the check cannot drift). Not on website/advanced.html and
#     website/licenses.html, the pages written for someone who knows computers
#     (the license page names kernels and protocols, which the law asks it to);
#   - AurBridge, PC or PCs, wifi (it is "Wi-Fi"), World Wide Web,
#     "That's normal", "Start installing", "Darker screen", yellow, "--"
#     between words (it is "—"), or a button that says only "Close".
# Where it looks: the visible text, alt, title and aria-label of
# website/*.html and the help pages (not inside data-ui="...": those quote
# another screen, and are checked at their source); the string literals of the installer
# (wizard.c, fix_win.c), the staging screens (install.c, screen.c) and
# the whole desktop shell (src/aurshell/*.c and layouts/*.c: first start,
# Settings, Wi-Fi, Bluetooth, the four looks), outside comments and log lines.
# Standard library only.
exec python3 -I - "$(cd "$(dirname "$0")/.." && pwd)" "$@" <<'PY'
import glob, html, html.parser, os, re, sys

repo, args = sys.argv[1], sys.argv[2:]

def easy_words():
    with open(os.path.join(repo, "docs", "EASY.md"), encoding="utf-8") as f:
        m = re.search(r"may not\s+say,[^*]*\*(.+?)\*", f.read(), re.S)
    words = [w.strip(" .\n") for w in m.group(1).replace("\n", " ").split(",")] if m else []
    return [w for w in words if w]

EASY = easy_words()
# Windows' own words, quoted so she recognises the screen. Not ours to change.
WINDOWS_SAYS = ["Windows protected your PC", "might put your PC at risk",
                "couldn\u2019t verify if this file is safe", "couldn't verify if this file is safe",
                # the firmware's own screen, quoted so she can find the line to change (the
                # Secure Boot page's sketch): its words, not ours
                "UEFI Firmware Settings", "Allow Microsoft 3rd Party UEFI CA"]

VOICE = [
    (r"\baurbridge\b", "AurBridge (it is AurOS Setup)", re.I),
    (r"\bPCs?\b", "PC (it is this computer)", 0),
    (r"\bwi-?fi\b", "wifi (it is Wi-Fi)", re.I),
    (r"\bworld wide web\b", "World Wide Web", re.I),
    (r"\bthat[’']s normal\b", "That's normal", re.I),
    (r"\bstart installing\b", "Start installing (it is Install AurOS)", re.I),
    (r"\bdarker screen\b", "Darker screen", re.I),
    (r"\byellow\b", "yellow (say numbered circles, never a color)", re.I),
    (r"\w ?-- ?\w", '"--" (it is "—")', 0),
]

# What the computer is made of is not hers to know (DESIGN-DIRECTION B-9: "starts up an older
# way", not BIOS; the helper pages, advanced.html and licenses.html, have the names).
HARDWARE = [
    (r"\bBIOS\b", "BIOS (say how the computer starts up)", 0),
    (r"\bUEFI\b", "UEFI (say how the computer starts up)", 0),
    (r"\bNTFS\b", "NTFS (say Windows' drive)", 0),
    (r"\b(MT|RTL|BCM|QCA|AR9)\d{3,5}[A-Z]{0,3}\b", "a chip's name (the helper page names them)", 0),
]

def voice_hits(text, easy):
    for q in WINDOWS_SAYS:
        text = text.replace(q, " ")
    out = []
    for pat, why, fl in VOICE + (HARDWARE if easy else []):
        # "Wi-Fi" itself matches the wifi pattern; only other spellings count.
        if any(m.group(0) != "Wi-Fi" for m in re.finditer(pat, text, fl)):
            out.append(why)
    if easy:
        for w in EASY:
            if re.search(r"(?<![\w-])" + re.escape(w) + r"(?![\w-])", text, re.I):
                out.append(f'"{w}" (docs/EASY.md rule 2)')
    return out

class Visible(html.parser.HTMLParser):
    """Visible text with its line, plus alt/title/aria-label, and the text
    of every button or link (for a bare Close)."""
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.bits, self.skip, self.press = [], 0, []
        self.quote = 0      # inside data-ui: another screen's words, linted at their source
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag in ("script", "style", "template"):
            self.skip += 1
        if a.get("data-ui") is not None or self.quote:
            self.quote += 1
        for k in ("alt", "title", "aria-label"):
            if a.get(k):
                self.bits.append((self.getpos()[0], a[k]))
        if tag in ("button", "a"):
            self.press.append([self.getpos()[0], ""])
    def handle_endtag(self, tag):
        if tag in ("script", "style", "template") and self.skip:
            self.skip -= 1
        if self.quote:
            self.quote -= 1
        if tag in ("button", "a") and self.press:
            line, text = self.press.pop()
            if " ".join(text.split()) == "Close":
                self.bits.append((line, "\0bare-close"))
    def handle_data(self, data):
        if not self.skip and not self.quote and data.strip():
            self.bits.append((self.getpos()[0], data))
            for p in self.press:
                p[1] += data

LOGLINE = re.compile(r"\b(f?printf|perror|syslog|OutputDebugString\w*|log_\w*|\w*_log|LOG\w*|trace\w*|dbg\w*|say|strstr|strcasestr)\s*\(|^\s*#\s*(include|if)")

def c_strings(src):
    """(line, text) of each string literal outside comments and log lines."""
    code = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    # What goes to stderr is for whoever runs the program, not for her: the whole statement,
    # however many lines the format string takes.
    code = re.sub(r"(?:fprintf\(\s*stderr\b|\bfputs\().*?\)\s*;", lambda m: "\n" * m.group(0).count("\n"), code, flags=re.S)
    out = []
    raw = src.split("\n")
    for n, line in enumerate(code.split("\n"), 1):
        # a test's data: the engine's own sentences, quoted to prove a mapping (the marker is a comment)
        if LOGLINE.search(line) or (n <= len(raw) and "wordlint:ignore" in raw[n - 1]):
            continue
        for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*)"', line):
            s = m.group(1)
            # Not words she reads: names, paths and flags (no space, not
            # one capitalised word like "Close"), and key=value log lines.
            if not re.search(r"\s", s) and not re.fullmatch(r"[A-Z][a-z]+", s):
                continue
            if re.search(r"\w=%", s):
                continue
            # Not on her screen: XML, a command line the shell runs, the list of files a theme
            # renders, a string the program matches in another program's output.
            if re.match(r"\s*</?\w", s) or "/dev/null" in s or re.search(r"\bexec \w", s):
                continue
            if re.fullmatch(r"[\w-]+\.\w+( [\w-]+\.\w+)*", s):
                continue
            if re.search(r"[A-Za-z]", s):
                out.append((n, bytes(s, "utf-8").decode("unicode_escape", "replace")
                            if "\\u" in s else s))
    return out

def surfaces():
    if args:
        return [os.path.abspath(a) for a in args]
    pats = ["website/*.html", "rootfs/usr/share/auros/help/*.html",
            "src/aurbridge/wizard.c", "src/aurbridge/fix_win.c", "src/aurbridge/stub/stub.c",
            "src/aurstage/install.c", "src/aurscreen/screen.c",
            "src/aurshell/*.c", "src/aurshell/layouts/*.c"]
    return sorted(f for p in pats for f in glob.glob(os.path.join(repo, p)))

hits = 0
for f in surfaces():
    rel = os.path.relpath(f, repo)
    try:
        src = open(f, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"{rel}: cannot read ({e})"); hits += 1; continue
    if f.endswith(".html"):
        if re.search(r'http-equiv="refresh"', src, re.I):
            continue                               # a redirect stub, nothing to read
        v = Visible(); v.feed(src)
        easy = not rel.endswith(("website/advanced.html", "website/licenses.html"))   # pages for someone who knows computers
        items = v.bits
    else:
        easy, items = True, c_strings(src)
    for line, text in items:
        found = ['"Close" alone (say where she lands)'] if text == "\0bare-close" else \
                ['"Close" alone (say where she lands)'] if (not f.endswith(".html") and text == "Close") else \
                voice_hits(html.unescape(text), easy)
        for why in found:
            hits += 1
            shown = " ".join(text.split())[:70] if text != "\0bare-close" else "Close"
            print(f"{rel}:{line}: {why}: {shown}")
print(f"\nwordlint: {hits} problem{'s' if hits != 1 else ''}")
sys.exit(1 if hits else 0)
PY
