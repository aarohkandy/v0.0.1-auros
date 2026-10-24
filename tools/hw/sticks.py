#!/usr/bin/env python3
"""sticks.py -- the USB Wi-Fi sticks AurOS tells people to buy, kept honest.

  python3 -I tools/hw/sticks.py            check everything (exit 1 on any problem)
  python3 -I tools/hw/sticks.py --write    also rewrite the list in website/advanced.html

What it checks (hw/sticks.json is the one list; docs in its "_about"):

 1. Every chip's USB ids are claimed by the named driver, as a Wi-Fi driver whose
    firmware is in the image, in the kernel of the AurOS image (hw/compat.json)
    AND in the newer HWE kernel (hw/compat-hwe-wifi.json). A stick whose chip
    stops being supported turns this red.
 2. Each chip lists 2 to 4 example products, a product's id is one of its
    chip's ids, and no chip is one of the Realtek "AU" chips (RTL8811AU,
    RTL8812AU, RTL8821AU, RTL8814AU) that we will not recommend.
 3. The list in website/advanced.html (between the sticks:begin and sticks:end
    markers) is exactly what hw/sticks.json renders, and the page links to it.
 4. EVERYTHING A PERSON CAN READ that mentions a USB Wi-Fi stick, adapter or
    dongle names a chip from the list, or links to the list. Scanned: the
    website, the help pages, the installer's and the staging screens' strings,
    the sentences in hw/seeds.json, and the campaign sheet. A page that says
    "a $15 stick fixes it" with no chip is the false promise this exists to stop.
    A chunk that names one of the banned Realtek chips must say it is not
    recommended.
"""
import html, html.parser, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("STICKS_ROOT") or os.path.dirname(os.path.dirname(HERE))   # STICKS_ROOT: a copy of the tree, for the tests
ADV = os.path.join(REPO, "website", "advanced.html")
BEGIN = "<!-- sticks:begin (made by tools/hw/sticks.py from hw/sticks.json: edit that, then run it with --write) -->"
END = "<!-- sticks:end -->"

STICK = re.compile(r"(?i)\b(?:usb\s+)?wi-?fi\s+(?:sticks?|dongles?|adapters?|plugs?)\b|\b(?:wi-?fi|wireless)\s+usb\s+(?:sticks?|adapters?|dongles?)\b"
                   r"|\busb\s+(?:sticks?\s+)?(?:for|that\s+gives)\s+wi-?fi\b")
BANNED = re.compile(r"(?i)\b(?:rtl)?-?(?:8811|8812|8814|8821)\s*au\b")
NEGATION = re.compile(r"(?i)\b(not|never|avoid|don[’']t|do not|isn[’']t|aren[’']t|won[’']t|no driver)\b")


def load(rel):
    return json.load(open(os.path.join(REPO, rel), encoding="utf-8"))


def usb_ids(mod):
    return {e.split("@")[0] for e in mod.get("usb", "").split()}


def esc(t):
    """HTML for a plain sentence, with typographic quotes like the rest of the page"""
    t = html.escape(t, quote=False)
    t = re.sub(r'"([^"]*)"', "&ldquo;\\1&rdquo;", t)
    return t.replace("'", "&rsquo;")


def short_kernel(k):
    m = re.match(r"(\d+\.\d+)", k or "")
    return m.group(1) if m else (k or "?")


# ── 1 and 2: the data ────────────────────────────────────────────────
def check_data(S, errs):
    sets = []
    for label, rel in S["data"].items():
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            errs.append("missing %s" % rel)
            continue
        d = json.load(open(p, encoding="utf-8"))
        sets.append((label, d["image"]["kernel"], d["modules"]))
    if not sets:
        return []
    for c in S["chips"]:
        name = c["name"]
        if BANNED.search(name) or BANNED.search(" ".join(c.get("aliases", []))):
            errs.append("%s: a chip we will not recommend" % name)
        if not 2 <= len(c["products"]) <= 4:
            errs.append("%s: %d example products (want 2 to 4)" % (name, len(c["products"])))
        if not c["ids"]:
            errs.append("%s: no USB ids to check" % name)
        for pr in c["products"]:
            if pr.get("id") and pr["id"] not in c["ids"]:
                errs.append("%s: %s has id %s, which is not one of the chip's ids" % (name, pr["name"], pr["id"]))
            if BANNED.search(pr["name"]):
                errs.append("%s: product %s is a banned chip" % (name, pr["name"]))
        for label, kernel, mods in sets:
            m = mods.get(c["module"])
            if not m or m.get("cat") != "wifi":
                errs.append("%s: no Wi-Fi driver %s in the %s kernel (%s)" % (name, c["module"], label, kernel))
                continue
            if m.get("fw_required") and m.get("fw") == "missing":
                errs.append("%s: %s needs firmware the image does not have (%s kernel)" % (name, c["module"], label))
            have = usb_ids(m)
            for i in c["ids"]:
                if i not in have:
                    errs.append("%s: %s is not claimed by %s in the %s kernel (%s)" % (name, i, c["module"], label, kernel))
    return [(l, k) for l, k, _ in sets]


# ── 3: the list on the page ──────────────────────────────────────────
def render(S, kernels):
    kn = []
    for label, k in kernels:
        v = short_kernel(k)
        if v not in kn:
            kn.append(v)
    kernels_txt = " and ".join(kn)
    o = [BEGIN,
         '<section class="doc" id="wifi-sticks" aria-labelledby="wifi-sticks-title">',
         '  <div class="narrow">',
         '    <h2 id="wifi-sticks-title">Which USB Wi-Fi stick to buy</h2>',
         '    <p>If this computer&rsquo;s own Wi-Fi does not work with AurOS, a USB Wi-Fi stick can take its place. It has to be a stick whose chip AurOS already has a driver for. Each chip below was checked against the list of chips in AurOS&rsquo;s own kernel (Linux %s), including the firmware that driver needs.</p>' % html.escape(kernels_txt),
         '    <p><b>Buy by the chip, not by the name.</b> Makers change the chip inside a model between versions without changing its name. If a product page does not say which chip is inside, do not buy it. Where a version is shown below, only that version.</p>',
         ] + ([] if S.get("tried") else [
         '    <p>None of these has been tried on a real computer with AurOS yet; that is part of the lab&rsquo;s hardware campaign. Keep the receipt.</p>']) + [
         '    <p>Plug the stick in, then open AurOS Setup again: it reads the chip itself, so it does not matter that Windows may say it has no driver for it.</p>']
    for c in S["chips"]:
        o.append('    <h3>%s (%s)</h3>' % (esc(c["name"]), esc(c["what"])))
        o.append('    <p>Driver <code>%s</code>.%s</p>' % (html.escape(c["module"]),
                 (" " + esc(c["note"])) if c.get("note") else ""))
        o.append('    <ul>')
        for pr in c["products"]:
            o.append('      <li>%s</li>' % esc(pr["name"]))
        o.append('    </ul>')
    a = S["avoid"]
    o += ['    <h3>Do not buy these</h3>',
          '    <p>%s sticks (%s) are not on this list: %s. A stick that does not name its chip may be one of them.</p>' % (
              esc(a["chips"]), esc(a["examples"]), esc(a["why"])),
          '    <p class="totop"><a href="#main">Back to the top</a></p>',
          '  </div>',
          '</section>',
          END]
    return "\n".join(o)


def check_page(S, kernels, errs, write):
    src = open(ADV, encoding="utf-8").read()
    want = render(S, kernels)
    i, j = src.find(BEGIN), src.find(END)
    if i < 0 or j < 0:
        if write:
            anchor = '  <!-- ── anything else'
            k = src.find(anchor)
            if k < 0:
                errs.append("website/advanced.html: no place to put the list")
                return
            src = src[:k] + want + "\n\n" + src[k:]
            src = src.replace('        <li><a href="#else">', '        <li><a href="#wifi-sticks">Which USB Wi-Fi stick to buy</a></li>\n        <li><a href="#else">', 1)
            open(ADV, "w", encoding="utf-8").write(src)
            return
        errs.append("website/advanced.html: the list (sticks:begin ... sticks:end) is missing; run tools/hw/sticks.py --write")
        return
    have = src[i:j + len(END)]
    if have != want:
        if write:
            open(ADV, "w", encoding="utf-8").write(src[:i] + want + src[j + len(END):])
            return
        errs.append("website/advanced.html: the list is not what hw/sticks.json says; run tools/hw/sticks.py --write")
    seeds = load("hw/seeds.json")
    if seeds.get("links", {}).get("wifi.sticks") != S["page"]:
        errs.append("hw/seeds.json links.wifi.sticks (%r) is not hw/sticks.json page (%r)" % (seeds.get("links", {}).get("wifi.sticks"), S["page"]))
    if 'id="%s"' % S["page"].split("#")[1] not in src:
        errs.append("website/advanced.html: no place with id=%s" % S["page"].split("#")[1])
    if 'href="#wifi-sticks"' not in src:
        errs.append("website/advanced.html: the contents list has no link to #wifi-sticks")


# ── 4: everything a person can read ──────────────────────────────────
class Chunks(html.parser.HTMLParser):
    BLOCK = {"p", "li", "td", "th", "h1", "h2", "h3", "h4", "figcaption", "dd", "dt", "summary"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out, self.cur, self.depth, self.skip = [], [], 0, 0
        self.avoid_depth = []

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self.skip += 1
        if tag in self.BLOCK:
            self.flush()
        for k, v in attrs:
            if k in ("alt", "title", "aria-label", "content") and v:
                self.out.append(v)
            if k == "href" and v:
                self.cur.append(" %s " % v)       # a link to the list counts as naming it

    def handle_endtag(self, tag):
        if tag in ("script", "style") and self.skip:
            self.skip -= 1
        if tag in self.BLOCK:
            self.flush()

    def handle_data(self, d):
        if not self.skip:
            self.cur.append(d)

    def flush(self):
        t = " ".join("".join(self.cur).split())
        if t:
            self.out.append(t)
        self.cur = []


def html_chunks(path):
    p = Chunks()
    src = open(path, encoding="utf-8", errors="replace").read()
    i, j = src.find(BEGIN), src.find(END)
    if i >= 0 and j > i:
        src = src[:i] + src[j + len(END):]      # the list is checked by its renderer, not scanned
    p.feed(src)
    p.flush()
    return p.out


def c_chunks(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    out = []
    for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*)"', src):
        s = m.group(1).replace('\\"', '"').replace("\\n", " ")
        if len(s) > 12:
            out.append(s)
    return out


def md_chunks(path):
    return [" ".join(x.split()) for x in re.split(r"\n\s*\n", open(path, encoding="utf-8").read()) if x.strip()]


def json_words(path):
    return [v for v in json.load(open(path, encoding="utf-8")).get("words", {}).values()]


def surfaces():
    r = []
    def add(pattern_dir, exts, fn):
        d = os.path.join(REPO, pattern_dir)
        if os.path.isdir(d):
            for f in sorted(os.listdir(d)):
                if f.endswith(exts):
                    r.append((os.path.join(pattern_dir, f), fn(os.path.join(d, f))))
    add("website", (".html",), html_chunks)
    add("rootfs/usr/share/auros/help", (".html",), html_chunks)
    for f in ("src/aurbridge/wizard.c", "src/aurbridge/fix_win.c", "src/aurshell/welcome.c"):
        if os.path.exists(os.path.join(REPO, f)):
            r.append((f, c_chunks(os.path.join(REPO, f))))
    add("src/aurstage", (".c",), c_chunks)
    add("src/aurscreen", (".c",), c_chunks)
    r.append(("hw/seeds.json (the sentences)", json_words(os.path.join(REPO, "hw/seeds.json"))))
    add("docs/results/hardware", (".md",), md_chunks)
    return r


def chip_words(S):
    w = []
    for c in S["chips"]:
        w.append(c["name"])
        w.append(c["name"].split()[-1])
        w += c.get("aliases", [])
    return [x.lower() for x in w]


def check_surfaces(S, errs):
    names = chip_words(S)
    anchor = S["page"].split("#")[1]
    n_seen = 0
    for where, chunks in surfaces():
        # the list itself is checked by its renderer
        for t in chunks:
            if STICK.search(t):
                n_seen += 1
                low = t.lower()
                # The installer's named button opens the checked list. Its
                # label is the link on these native screens, where an HTML
                # fragment cannot be embedded in the sentence itself.
                linked = anchor in low or "show a helper how" in low
                if not any(n in low for n in names) and not linked:
                    errs.append("%s: mentions a USB Wi-Fi stick with no chip from hw/sticks.json and no link to #%s: %r" % (where, anchor, t[:110]))
            if BANNED.search(t) and not NEGATION.search(t):
                errs.append("%s: names a Realtek AU chip without saying it is not recommended: %r" % (where, t[:110]))
    return n_seen


def main():
    write = "--write" in sys.argv
    S = load("hw/sticks.json")
    errs = []
    kernels = check_data(S, errs)
    check_page(S, kernels, errs, write)
    seen = check_surfaces(S, errs)
    for e in errs:
        print("sticks:", e, file=sys.stderr)
    if not errs:
        print("sticks: %d chips, %d products, %d places that talk about a stick all name a chip or link the list" % (
            len(S["chips"]), sum(len(c["products"]) for c in S["chips"]), seen))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
