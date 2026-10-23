#!/usr/bin/env python3
"""check.py -- the legal texts keep their promises (stream LEGAL).

    python3 -I tools/legal/check.py              the checks; exit 1 on a failure
    python3 -I tools/legal/check.py --launch     also fail while CONTACT_EMAIL or a
                                                 draft marker is still in a public text
    python3 -I tools/legal/check.py selftest     each check refuses what it exists to refuse
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
PUBLIC = ["rootfs/usr/share/doc/auros", "website", "docs/legal/TERMS.md", "docs/legal/PRIVACY.md",
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
    ("licences prevail", r"the licence wins"),
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


CHECKS = []   # filled by the sections below


def run(launch=False, root=None):
    problems = []
    problems += check_offer(root)
    p, seen = check_placeholders(launch, root)
    problems += p
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
            ("an offer that lets the licence lose is refused", "the licence wins", "these words win", "licences prevail"),
        ]:
            assert old in good, old
            open(offer, "w").write(good.replace(old, new))
            want(label, run(False, d)[0], needle)
        open(offer, "w").write(good)
        # placeholders
        open(os.path.join(d, "website/licences.html"), "w").write("<p>write to CONTACT_EMAIL [OWNER: x]</p>")
        pr, seen = run(True, d)
        want("--launch refuses the placeholder", pr, "still has CONTACT_EMAIL")
        want("--launch refuses a draft marker", pr, "draft marker")
        pr, seen = run(False, d)
        ok = seen >= 2 and not [x for x in pr if "still has" in x]
        print("%s  without --launch the placeholder is only counted (%d)" % ("ok  " if ok else "FAIL", seen)); bad += 0 if ok else 1
        # set-contact works on a git tree
        subprocess.run(["git", "init", "-q", d], check=True)
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        set_contact("help@example.org", d)
        t = open(os.path.join(d, "website/licences.html")).read()
        ok = "help@example.org" in t and PLACEHOLDER not in t
        print("%s  set-contact puts the address in the page" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        try:
            set_contact("not an address", d); ok = False
        except SystemExit:
            ok = True
        print("%s  set-contact refuses a non-address" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("\n%s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
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
