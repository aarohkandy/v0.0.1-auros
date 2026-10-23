#!/usr/bin/env python3
"""check.py -- the legal texts keep their promises (stream LEGAL).

    python3 -I tools/legal/check.py              the checks; exit 1 on a failure
    python3 -I tools/legal/check.py --launch     also fail while CONTACT_EMAIL or a
                                                 draft marker is still in a public text
    python3 -I tools/legal/check.py selftest     each check refuses what it exists to refuse
    python3 -I tools/legal/check.py image --status DPKG_STATUS [--root DIR]
                                                 the built image carries no Ubuntu branding (sources.yml runs this)
    python3 -I tools/legal/check.py set-contact ADDRESS
                                                 put the owner's mailbox in every public text

What it holds to (docs/legal/COMPLIANCE.md has the reasons):
  offer     the written offer of source says the same things wherever it appears
            and names the sources release by the rule build/sources keeps;
  claims    no public text uses a word we have promised not to (absolute data-safety
            and privacy claims, "free" without its limit, Ubuntu's marks as ours);
  marks     third-party names appear only in the forms the non-affiliation line allows;
  pages     the legal pages exist, are linked from every footer page, and carry the
            same non-affiliation sentence.
The CONTACT_EMAIL placeholder stands until the owner has a mailbox (set-contact).
"""
import os, re, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PLACEHOLDER = "CONTACT_EMAIL"
# every text a person can read that may carry the placeholder or a draft marker
PUBLIC = ["rootfs/usr/share/doc/auros", "rootfs/usr/share/auros/help", "website", "docs/legal/TERMS.md", "docs/legal/PRIVACY.md",
          "docs/legal/DISCLAIMER.md", "docs/legal/SOURCES.md", "SECURITY.md", "docs/SUPPORT.md"]
DRAFT_MARKERS = ["[OWNER", "[LAWYER", "[CHECK", "must be reviewed by a lawyer"]
# files that talk ABOUT the placeholder and must keep it
KEEP = ("tools/legal/", "docs/legal/COMPLIANCE.md", "docs/legal/README.md", "docs/handoff/", "docs/OWNER-SETUP.md")


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def public_files(root=None):
    root = root or ROOT
    out = []
    for p in PUBLIC:
        full = os.path.join(root, p)
        if os.path.isfile(full):
            out.append(p)
        elif os.path.isdir(full):
            for d, _, fs in os.walk(full):
                for f in sorted(fs):
                    if f.endswith((".html", ".md", ".txt", ".xml", "")) and not f.endswith((".woff2", ".png", ".jpg", ".svg", ".js", ".json", ".ttf", ".webp")):
                        out.append(os.path.relpath(os.path.join(d, f), root))
    return sorted(set(out))


def valid_address(a):
    return bool(re.fullmatch(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+", a)) and len(a) < 120


def set_contact(addr, root=None):
    """Replace the placeholder in every tracked text that carries it."""
    root = root or ROOT
    if not valid_address(addr):
        raise SystemExit("set-contact: %r is not an email address" % addr)
    n = 0
    files = subprocess.run(["git", "-C", root, "grep", "-lIw", PLACEHOLDER], capture_output=True, text=True).stdout.split()
    for f in files:
        if f.startswith(KEEP):
            continue
        p = os.path.join(root, f)
        t = open(p, encoding="utf-8").read()
        open(p, "w", encoding="utf-8").write(re.sub(r"\b%s\b" % PLACEHOLDER, addr, t))
        n += 1
        print("set-contact: %s" % f)
    print("set-contact: %d file(s). Commit and push." % n)
    return n


# ── the checks; each returns a list of problems ────────────────────

OFFER_MUST = [  # (what, regex) every written offer must contain
    ("the three-year promise", r"at least three years"),
    ("the offer to anyone who has a copy", r"anyone who has a copy of AurOS"),
    ("the physical-medium route at cost", r"cost of the\s+medium and the postage"),
    ("the sources release naming rule", r'"sources-" followed by the name of the AurOS\s+release'),
    ("the contact address", PLACEHOLDER + r"|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+"),
    ("licenses prevail", r"the license wins"),
]


def check_offer(root=None):
    root = root or ROOT
    p = os.path.join(root, "rootfs/usr/share/doc/auros/SOURCE-OFFER")
    if not os.path.isfile(p):
        return ["rootfs/usr/share/doc/auros/SOURCE-OFFER does not exist"]
    t = open(p, encoding="utf-8").read()
    out = []
    for what, rx in OFFER_MUST:
        if not re.search(rx, t):
            out.append("SOURCE-OFFER lacks %s" % what)
    if "@AUROS_RELEASES@" not in t:
        out.append("SOURCE-OFFER has no @AUROS_RELEASES@ (forge stamps the releases page into it)")
    # the tool that publishes must keep the rule the text states
    src = open(os.path.join(root, "build/sources"), encoding="utf-8").read() if os.path.isfile(os.path.join(root, "build/sources")) else ""
    wf = os.path.join(root, ".github/workflows/sources.yml")
    if os.path.isfile(wf) and 'sources-$TAG' not in open(wf, encoding="utf-8").read():
        out.append("sources.yml does not publish a release named sources-<tag>, which the offer promises")
    if src and "PART_BYTES = 1900 * 1024 * 1024" not in src:
        out.append("build/sources no longer keeps parts under GitHub's 2 GiB asset limit")
    return out


def check_placeholders(launch, root=None):
    root = root or ROOT
    out, seen = [], 0
    for f in public_files(root):
        t = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
        c = len(re.findall(r"\b%s\b" % PLACEHOLDER, t))
        seen += c
        if launch:
            if c:
                out.append("%s still has %s (%d)" % (f, PLACEHOLDER, c))
            for m in DRAFT_MARKERS:
                if m in t:
                    out.append("%s still has the draft marker %r" % (f, m))
    return out, seen



# ── the pages ──────────────────────────────────────────────────────

def real_pages(root):
    out = []
    d = os.path.join(root, "website")
    if not os.path.isdir(d):
        return out
    for f in sorted(os.listdir(d)):
        if f.endswith(".html"):
            t = open(os.path.join(d, f), encoding="utf-8").read()
            if not re.search(r'http-equiv="refresh"', t, re.I):
                out.append((f, t))
    return out


def check_pages(root):
    """The four legal pages exist and are the Markdown's; every page with a footer or a
    download link points to them; the notice files are the ones their generators write."""
    out = []
    for tool, label in (("mkpages.py", "the legal pages and footers"), ("notices.py", "the notice files")):
        r = subprocess.run([sys.executable, "-I", os.path.join(root, "tools/legal", tool), "--check"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            out.append("%s are not what tools/legal/%s writes: %s" % (label, tool, (r.stdout + r.stderr).strip().replace("\n", "; ")[:240]))
    for f, t in real_pages(root):
        if "releases/latest/download/AurOS-Setup.exe" in t and 'href="licenses.html"' not in t:
            out.append("website/%s has a download link and no link to licenses.html (GPLv3 6(d), COMPLIANCE LIC-2)" % f)
        if '<footer class="foot">' in t:
            for need in ("licenses.html", "terms.html", "disclaimer.html", "privacy.html"):
                if 'href="%s"' % need not in t.split('<footer class="foot">')[1]:
                    out.append("website/%s: the footer does not link %s" % (f, need))
            if "is not made, endorsed or supported by Canonical" not in t or "has not been authorized, sponsored, or otherwise approved by Apple Inc." not in t:
                out.append("website/%s: the footer lacks the non-affiliation paragraph (TM-3)" % f)
    return out


# ── honest words ───────────────────────────────────────────────────

# (regex, why). Absolute claims about data, privacy, money and tests that the legal review
# (COMPLIANCE LIA-2, PRI-1, WEB-3) took out, and that must not come back. Search is case-insensitive.
BANNED = [
    (r"all (your|her) files stay", "an absolute: 'Windows and your files are kept' (LIA-2)"),
    (r"windows and all your files", "an absolute: 'Windows and your files are kept' (LIA-2)"),
    (r"\buntouched\b", "an absolute: 'kept' or 'stay on this computer' (LIA-2)"),
    (r"(kept|keeps|keep) (windows and your files )?(just )?(exactly )?as (it|they) (was|were)", "an absolute: nothing on a drive stays exactly as it was (LIA-2)"),
    (r"exactly as (it|they) (was|were)", "an absolute (LIA-2)"),
    (r"byte-identical every time", "an absolute: say what the tests show (LIA-2)"),
    (r"nothing about you is collected", "GitHub logs every visitor (PRI-1)"),
    (r"\bwe collect nothing\b", "unqualified; GitHub logs visitors (PRI-1)"),
    (r"nothing to pay,? now or later", "paid extras are planned (WEB-3)"),
    (r"completely safe|100% safe|risk-free|\bno risk\b|zero risk", "no change to a drive is free of risk (LIA-2)"),
]
# Texts a person reads. C files: only lines that are not comments.
TEXT_GLOBS = ["website", "rootfs/usr/share/auros/help", "README.md", "docs/legal/TERMS.md", "docs/legal/PRIVACY.md",
              "docs/legal/DISCLAIMER.md", "docs/legal/LICENSES.md", "rootfs/usr/share/doc/auros"]
CODE_TEXTS = ["src/aurbridge/wizard.c", "src/aurbridge/fix_win.c", "src/aurstage/install.c", "src/aurscreen/screen.c",
              "src/aurshell/welcome.c"]
# Phrases that are still in someone else's file, handed over in docs/handoff/streams/LEGAL.md.
# Each ends on its date: after it, the phrase is a failure like any other. (file, regex index, owner, until)
PENDING = [
    ("src/aurbridge/wizard.c", r"all (your|her) files stay", "stream B: Ready and Welcome strings", "2026-11-30"),
    ("src/aurbridge/wizard.c", r"windows and all your files", "stream B: Ready and Welcome strings", "2026-11-30"),
    ("src/aurscreen/screen.c", r"exactly as (it|they) (was|were)", "stream A2: the put-back screen", "2026-11-30"),
]


def _strip(path, text):
    if path.endswith((".html", ".md", ".txt")) or "/SOURCE-OFFER" in path:
        return re.sub(r"<!--.*?-->", "", text, flags=re.S)
    # C: only the sentences a person reads (string literals with a space in them), not
    # comments and not tokens like "untouched" that the programs pass between themselves
    code = "\n".join(l for l in text.split("\n") if not re.match(r"\s*(/\*|\*|//)", l))
    return "\n".join(m.group(1) for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*\s(?:[^"\\\n]|\\.)*)"', code))


def _files(root, globs):
    out = []
    for g in globs:
        full = os.path.join(root, g)
        if os.path.isfile(full):
            out.append(g)
        elif os.path.isdir(full):
            for d, _, fs in os.walk(full):
                for f in sorted(fs):
                    if f.endswith((".html", ".md", ".txt", "")) and not f.endswith((".woff2", ".png", ".jpg", ".ttf", ".js", ".json", ".svg", ".webp", ".css")):
                        out.append(os.path.relpath(os.path.join(d, f), root))
    return sorted(set(out))


def check_claims(root, today=None):
    import datetime
    today = today or datetime.date.today().isoformat()
    out = []
    used = set()
    for rel in _files(root, TEXT_GLOBS) + CODE_TEXTS:
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        t = open(p, encoding="utf-8", errors="replace").read()
        if rel.startswith("website/") and re.search(r'http-equiv="refresh"', t, re.I):
            continue
        for rx, why in BANNED:
            for m in re.finditer(rx, _strip(rel, t), re.I):
                allowed = [x for x in PENDING if x[0] == rel and x[1] == rx]
                if allowed:
                    used.add((rel, rx))
                    if today > allowed[0][3]:
                        out.append("%s says %r, past its date (%s): %s" % (rel, m.group(0), allowed[0][3], allowed[0][2]))
                else:
                    out.append("%s says %r: %s" % (rel, m.group(0), why))
    return out


# ── marks ──────────────────────────────────────────────────────────

MARK_WORDS = r"ubuntu|canonical|kubuntu|mozilla|firefox"
LOGO_NAMES = r"(ubuntu|canonical|kubuntu|mozilla|firefox|microsoft|windows[-_]?logo|apple|google|chrome|edge)[-_.a-z0-9]*\.(png|jpe?g|svg|webp|ico|woff2?)$"


def check_marks_site(root):
    """Third-party names appear in running text and nowhere that would pass them off as AurOS's
    own: not in a page title, a heading of level 1, a button or a link's address, and no logo file."""
    import html.parser
    out = []
    ad = os.path.join(root, "website/assets")
    if os.path.isdir(ad):
        for d, _, fs in os.walk(ad):
            for f in fs:
                if re.search(LOGO_NAMES, f, re.I):
                    out.append("website/assets: %s looks like another company's logo (text only, never logos: TM-3)" % os.path.relpath(os.path.join(d, f), root))
    class P(html.parser.HTMLParser):
        def __init__(s):
            super().__init__(convert_charrefs=True); s.stack = []; s.hits = []
        def handle_starttag(s, tag, attrs):
            a = dict(attrs)
            s.stack.append((tag, a.get("class", "")))
            for k in ("href", "src"):
                v = a.get(k, "")
                if v and re.search(MARK_WORDS, v, re.I) and not v.startswith(("https://github.com/@AUROS_REPO@",)):
                    s.hits.append("%s=%s" % (k, v))
        def handle_endtag(s, tag):
            while s.stack:
                if s.stack.pop()[0] == tag:
                    break
        def handle_data(s, d):
            if re.search(MARK_WORDS, d, re.I):
                for tag, cls in s.stack:
                    if tag in ("title", "h1", "button") or (tag == "a" and re.search(r"\b(get|pill|btn2)\b", cls)):
                        s.hits.append("<%s> %s" % (tag, d.strip()[:40])); break
    for f, t in real_pages(root):
        p = P(); p.feed(t)
        for h in p.hits:
            out.append("website/%s: a third-party name where it would read as ours: %s (COMPLIANCE LIC-5)" % (f, h))
    return out


IMAGE_DENY = (r"^(ubuntu-(wallpapers|artwork|desktop|session|settings|docs|sounds|mono|minimal|standard|server|restricted-addons)|"
              r"yaru-theme-|plymouth-theme-ubuntu|ubuntu-.*logo|kubuntu-|xubuntu-|lubuntu-|ubuntu-release-upgrader)")


def check_image(status_path, rootdir=None):
    """The built image carries no Ubuntu artwork or branding packages, and does not name itself Ubuntu."""
    sys.path.insert(0, os.path.join(ROOT, "build"))
    import importlib.machinery, importlib.util
    spec = importlib.util.spec_from_loader("srcs", importlib.machinery.SourceFileLoader("srcs", os.path.join(ROOT, "build/sources")))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    out = []
    for p in m.parse_status(open(status_path, encoding="utf-8", errors="replace").read()):
        if re.search(IMAGE_DENY, p["bin"]):
            out.append("the image carries %s, an Ubuntu branding package (LIC-5)" % p["bin"])
    warn = []
    if rootdir:
        def rd(rel):
            q = os.path.join(rootdir, rel)
            return open(q, encoding="utf-8", errors="replace").read() if os.path.isfile(q) else None
        osr = rd("usr/lib/os-release")
        if osr is None:
            out.append("no usr/lib/os-release in the image sample")
        else:
            for k in ("NAME", "PRETTY_NAME", "ID"):
                v = re.search(r'^%s="?([^"\n]*)' % k, osr, re.M)
                if v and re.search(r"ubuntu", v.group(1), re.I):
                    out.append("os-release %s=%s: the image names itself Ubuntu (TM-1)" % (k, v.group(1)))
        iss = rd("etc/issue")
        if iss is not None and re.search(r"ubuntu", iss, re.I):
            out.append("/etc/issue names Ubuntu (TM-1)")
        lsb = rd("etc/lsb-release")
        if lsb is not None and re.search(r"^DISTRIB_ID=Ubuntu", lsb, re.M):
            warn.append("/etc/lsb-release still says DISTRIB_ID=Ubuntu (TM-1 item 1: dpkg-divert it; build/forge, stream C)")
    return out, warn



# What must be in place before the public link (--launch). Each line is a promise the published
# Terms make, or the review gate of docs/legal/README.md.
LAUNCH_NEEDS = [
    ("src/aurbridge/wizard.c", r"By pressing Install AurOS you agree to the AurOS Terms", "the Ready page's agreement line (TERMS 4)"),
    ("src/aurbridge/wizard.c", r"Any change to a drive carries some risk", "the Ready page's risk sentence (LIA-2)"),
    ("src/aurbridge/aurbridge-info.rc", r"\b200\s+RCDATA", "the NOTICES resource in AurOS-Setup.exe (LIC-3)"),
    ("src/aurbridge/aurbridge-info.rc", r"\b202\s+RCDATA", "the Terms resource in AurOS-Setup.exe (LIA-1)"),
    ("docs/legal/REVIEWS.md", r"(?s)^(?!.*NOT DONE)", "the two skeptic reviews, all findings answered (REVIEWS.md has no 'NOT DONE')"),
]


def check_launch_needs(root):
    out = []
    for rel, rx, what in LAUNCH_NEEDS:
        p = os.path.join(root, rel)
        t = open(p, encoding="utf-8", errors="replace").read() if os.path.isfile(p) else ""
        if not os.path.isfile(p) or not re.search(rx, t):
            out.append("not yet: %s (%s)" % (what, rel))
    return out

CHECKS = [check_pages, check_claims, check_marks_site]


def run(launch=False, root=None):
    problems = []
    problems += check_offer(root)
    p, seen = check_placeholders(launch, root)
    problems += p
    if launch:
        problems += check_launch_needs(root or ROOT)
    for fn in CHECKS:
        problems += fn(root or ROOT)
    return problems, seen


def selftest():
    bad = 0
    def want(label, problems, needle):
        nonlocal bad
        ok = any(needle in x for x in problems)
        print("%s  %s" % ("ok  " if ok else "FAIL", label))
        bad += 0 if ok else 1
    def tree():
        d = tempfile.mkdtemp(prefix="legalcheck.")
        for rel in ("rootfs/usr/share/doc/auros", "website", "build", ".github/workflows"):
            os.makedirs(os.path.join(d, rel))
        shutil.copy(os.path.join(ROOT, "rootfs/usr/share/doc/auros/SOURCE-OFFER"), os.path.join(d, "rootfs/usr/share/doc/auros/SOURCE-OFFER"))
        shutil.copy(os.path.join(ROOT, "build/sources"), os.path.join(d, "build/sources"))
        shutil.copy(os.path.join(ROOT, ".github/workflows/sources.yml"), os.path.join(d, ".github/workflows/sources.yml"))
        return d
    d = tree()
    try:
        pr, _ = run(False, d)
        ok = not [x for x in pr if "SOURCE-OFFER" in x or "sources" in x]
        print("%s  the real offer passes its own checks" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        offer = os.path.join(d, "rootfs/usr/share/doc/auros/SOURCE-OFFER")
        good = open(offer).read()
        for label, old, new, needle in [
            ("an offer without the three years is refused", "at least three years", "a while", "three-year"),
            ("an offer without the medium route is refused", "cost of the\nmedium and the postage", "price", "physical-medium"),
            ("an offer that dropped the sources- rule is refused", '"sources-" followed by', '"x-" followed by', "naming rule"),
            ("an offer with no releases page placeholder is refused", "@AUROS_RELEASES@", "https://example.org", "@AUROS_RELEASES@"),
            ("an offer that lets the license lose is refused", "the license wins", "these words win", "licenses prevail"),
        ]:
            assert old in good, old
            open(offer, "w").write(good.replace(old, new))
            want(label, run(False, d)[0], needle)
        open(offer, "w").write(good)
        # placeholders
        open(os.path.join(d, "website/licenses.html"), "w").write("<p>write to CONTACT_EMAIL [OWNER: x]</p>")
        pr, seen = run(True, d)
        want("--launch refuses the placeholder", pr, "still has CONTACT_EMAIL")
        want("--launch refuses while the installer lacks the agreement line", pr, "the Ready page's agreement line")
        want("--launch refuses a draft marker", pr, "draft marker")
        pr, seen = run(False, d)
        ok = seen >= 2 and not [x for x in pr if "still has" in x]
        print("%s  without --launch the placeholder is only counted (%d)" % ("ok  " if ok else "FAIL", seen)); bad += 0 if ok else 1
        # set-contact works on a git tree
        subprocess.run(["git", "init", "-q", d], check=True)
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        set_contact("help@example.org", d)
        t = open(os.path.join(d, "website/licenses.html")).read()
        ok = "help@example.org" in t and PLACEHOLDER not in t
        print("%s  set-contact puts the address in the page" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        try:
            set_contact("not an address", d); ok = False
        except SystemExit:
            ok = True
        print("%s  set-contact refuses a non-address" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
    finally:
        shutil.rmtree(d, ignore_errors=True)
    # ── words, marks, image, pages ─────────────────────────────────
    d = tempfile.mkdtemp(prefix="legalcheck2.")
    try:
        def put(rel, text):
            q = os.path.join(d, rel); os.makedirs(os.path.dirname(q), exist_ok=True); open(q, "w").write(text)
        put("website/a.html", "<html><body><p>Windows and your files are kept.</p></body></html>")
        want("a clean page has no banned claim", [] if not check_claims(d) else ["x"], "x") if False else None
        ok = not check_claims(d); print("%s  a clean page passes the claims check" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        for phrase, why in (("Windows and all your files stay.", "all your files"), ("Your files stay untouched.", "untouched"),
                            ("Nothing about you is collected.", "Nothing about you"), ("Kept just as they were.", "just as they were"),
                            ("There is nothing to pay, now or later.", "nothing to pay"), ("It is completely safe.", "completely safe"),
                            ("A change with no risk at all.", "no risk")):
            put("website/a.html", "<html><body><p>%s</p></body></html>" % phrase)
            want("the claims check refuses %r" % phrase, check_claims(d), why)
        put("website/a.html", "<html><body><!-- untouched --><p>fine</p></body></html>")
        ok = not check_claims(d); print("%s  a comment is not a claim" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        want("a pending phrase is refused once its date has passed", check_claims(ROOT, "2027-06-01"), "past its date")
        # marks
        put("website/a.html", "<html><head><title>Ubuntu Remix</title></head><body><h1>Firefox</h1><p>built from Ubuntu</p><a class='get' href='x'>Get Firefox</a></body></html>")
        m = check_marks_site(d)
        want("a third-party name in a title is refused", m, "<title>"); want("... in a heading", m, "<h1>"); want("... on a Get button", m, "<a>")
        put("website/a.html", "<html><head><title>AurOS</title></head><body><p>Built from Ubuntu 24.04 LTS software; Firefox is a trademark of the Mozilla Foundation.</p></body></html>")
        ok = not check_marks_site(d); print("%s  running text may name them (nominative use)" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        put("website/assets/ubuntu-logo.svg", "<svg/>")
        want("a logo file named for another company is refused", check_marks_site(d), "logo")
        # the image
        put("st", "Package: ubuntu-wallpapers\nStatus: install ok installed\nVersion: 1\n\nPackage: bash\nStatus: install ok installed\nVersion: 1\n")
        want("the image check refuses an Ubuntu branding package", check_image(os.path.join(d, "st"))[0], "ubuntu-wallpapers")
        put("st2", "Package: bash\nStatus: install ok installed\nVersion: 1\n\nPackage: ubuntu-keyring\nStatus: install ok installed\nVersion: 1\n")
        ok = not check_image(os.path.join(d, "st2"))[0]; print("%s  ubuntu-keyring and bash are fine" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        put("r/usr/lib/os-release", 'NAME="Ubuntu"\nPRETTY_NAME="Ubuntu 24.04"\nID=ubuntu\n'); put("r/etc/lsb-release", "DISTRIB_ID=Ubuntu\n")
        b, w = check_image(os.path.join(d, "st2"), os.path.join(d, "r"))
        want("the image check refuses an os-release that says Ubuntu", b, "os-release NAME"); want("... and warns about lsb-release", w, "lsb-release")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    # the pages and notices, on a copy of the real ones
    d = tempfile.mkdtemp(prefix="legalcheck3.")
    try:
        for rel in ("website", "docs/legal", "tools/legal", "rootfs/usr/share/doc/auros", "src/aurwl/gen", "LICENSE"):
            src = os.path.join(ROOT, rel); dst = os.path.join(d, rel)
            if os.path.isdir(src):
                shutil.copytree(src, dst, ignore=shutil.ignore_patterns("shots", "*.woff2", "__pycache__"))
            else:
                shutil.copy(src, dst)
        ok = not check_pages(d); print("%s  the real pages and notices are what their generators write" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        idx = os.path.join(d, "website/index.html"); good = open(idx).read()
        open(idx, "w").write(good.replace('href="licenses.html"', 'href="x.html"'))
        want("a page without the license link is refused", check_pages(d), "licenses.html")
        open(idx, "w").write(good)
        t = os.path.join(d, "docs/legal/TERMS.md"); gt = open(t).read()
        open(t, "w").write(gt.replace("AurOS itself is free.", "AurOS is free.", 1))
        want("a Terms page that is not the Markdown is refused", check_pages(d), "legal pages and footers")
        open(t, "w").write(gt)
        n = os.path.join(d, "rootfs/usr/share/doc/auros/THIRD-PARTY-NOTICES"); open(n, "a").write("x")
        want("a notice file that is not what is generated is refused", check_pages(d), "notice files")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("\n%s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
    if argv[:1] == ["image"]:
        a = argv[1:]
        st = a[a.index("--status") + 1] if "--status" in a else None
        rd = a[a.index("--root") + 1] if "--root" in a else None
        if not st:
            print(__doc__); return 2
        bad, warn = check_image(st, rd)
        for w in warn: print("legal: warning: " + w)
        for b in bad: print("legal: " + b)
        print("legal: image: %d problem(s)" % len(bad)); return 1 if bad else 0
    if argv[:1] == ["set-contact"]:
        if len(argv) != 2:
            print(__doc__); return 2
        set_contact(argv[1]); return 0
    launch = "--launch" in argv
    problems, seen = run(launch)
    for p in problems:
        print("legal: " + p)
    print("legal: %d problem(s); %s still stands in %s" % (len(problems), PLACEHOLDER, "public texts" if seen else "no public text"))
    if seen and not launch:
        print("legal: (%d use(s) of %s: the owner's mailbox is not set yet; --launch refuses this)" % (seen, PLACEHOLDER))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
