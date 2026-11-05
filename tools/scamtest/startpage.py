#!/usr/bin/env python3
"""startpage.py -- does "Internet" open onto something she can press?

Firefox with no homepage opens a search box, and the only way to Gmail is
to type gmail.com. rootfs/etc/firefox/policies/policies.json now sets the
homepage to a page on the computer, rootfs/usr/share/auros/start/index.html:
big buttons, no typing. This pins that, and the other things she meets in
the browser's first minute, by what Firefox itself does with the file
(read in Firefox esr140's Policies.sys.mjs and ContentDispatchChooser.sys.mjs):

  - Homepage: a file:// page that exists in the image, locked, and used at
    start-up (StartPage homepage-locked). A policy with a URL also puts the
    Home button on the toolbar, which is her way back.
  - no welcome tour, privacy notice, data-sharing bar, "make Firefox your
    default" question or sign-in offer on the first start.
  - a mailto: link handed to Firefox by the desktop opens Gmail, asking
    nothing. Firefox DROPS a web handler whose address is not https (so a
    page on the computer cannot be the handler), and a handler that says
    "use the system default" hands the link back to the desktop, which hands
    it to Firefox again: a loop. Firefox forces a chooser window for every
    link from outside unless the preferred handler is a web site.
  - the page: under 50 KB, nothing loaded from anywhere, no script, only
    https addresses behind its buttons, very large buttons and type, words
    she uses, and a button for each of the things she asked for.

    python3 -I tools/scamtest/startpage.py [--prove]

--prove breaks each check's subject in a copy and insists it goes red.
Standard library only; no browser needed. Exit 0 when everything passes.
"""
import copy
import html.parser
import json
import os
import re
import sys
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
POLICY = os.path.join(ROOT, "rootfs", "etc", "firefox", "policies", "policies.json")
START = "/usr/share/auros/start/index.html"
PAGE = os.path.join(ROOT, "rootfs" + START)
BUTTONS = ["Gmail", "Outlook", "Video calls", "YouTube", "News", "Weather", "Shopping"]

# docs/EASY.md rule 2 -- the same list as firefoxpolicy.py's, for what she reads here.
JARGON = re.compile(
    r"\b(ssid|wpa|authenticat\w*|credential\w*|interface|adapter|daemon|partition|repositor\w*|"
    r"dependenc\w*|runtime|sandbox|sudo|binary|executable|directory|kernel|flatpak|parameter|"
    r"validat\w*|policy|policies|extension\w*|url|http\w*)\b", re.I)


def pol(doc):
    return doc.get("policies", {}) if isinstance(doc, dict) else {}


class Dup(Exception):
    pass


def no_dups(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise Dup(k)
        d[k] = v
    return d


class Page(html.parser.HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.tags, self.text, self.style, self._in = [], [], [], None
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs)))
        self._in = tag

    def handle_endtag(self, tag):
        self._in = None

    def handle_data(self, data):
        if self._in == "style":
            self.style.append(data)
        elif self._in not in ("script", "title") and data.strip():
            self.text.append(data.strip())


def need(cond, msg):
    return [] if cond else [msg]


# ── the policy ───────────────────────────────────────────────────────

def check_homepage(doc, page, raw):
    h = pol(doc).get("Homepage", {})
    errs = need(h.get("URL") == "file://" + START,
                "Homepage.URL must be file://%s, got %r" % (START, h.get("URL")))
    errs += need(h.get("Locked") is True, "Homepage.Locked must be true (nothing can change her start page)")
    errs += need(h.get("StartPage") == "homepage-locked",
                 "Homepage.StartPage must be homepage-locked: Firefox opens the homepage, not a blank search box")
    return errs


def check_page_exists(doc, page, raw):
    return need(os.path.isfile(PAGE), "the homepage file %s is not in the image's overlay" % PAGE)


def check_first_run(doc, page, raw):
    p, errs = pol(doc), []
    if p.get("OverrideFirstRunPage") != "":
        errs.append("OverrideFirstRunPage must be \"\": no welcome or privacy-notice tab")
    if p.get("UserMessaging", {}).get("SkipOnboarding") is not True:
        errs.append("UserMessaging.SkipOnboarding must be true: no welcome tour")
    if p.get("SkipTermsOfUse") is not True:
        errs.append("SkipTermsOfUse must be true: no data-sharing window")
    if p.get("DontCheckDefaultBrowser") is not True:
        errs.append("DontCheckDefaultBrowser must be true: no 'make Firefox your default' question")
    if p.get("DisableFirefoxAccounts") is not True:
        errs.append("DisableFirefoxAccounts must be true: no 'sign in to Firefox' offer")
    n = p.get("Preferences", {}).get("datareporting.policy.dataSubmissionPolicyBypassNotification", {})
    if n.get("Value") is not True or n.get("Status") != "locked":
        errs.append("datareporting.policy.dataSubmissionPolicyBypassNotification must be locked true: no data-choices bar")
    return errs


def check_mailto(doc, page, raw):
    m = pol(doc).get("Handlers", {}).get("schemes", {}).get("mailto")
    if not isinstance(m, dict):
        return ["Handlers.schemes.mailto is missing: a mailto: link from the desktop opens Firefox's chooser window"]
    errs = need(m.get("action") == "useHelperApp",
                "mailto action must be useHelperApp (useSystemDefault hands the link back to the desktop, "
                "which hands it to Firefox: a loop), got %r" % m.get("action"))
    errs += need(m.get("ask") is False, "mailto ask must be false: she is asked nothing")
    hs = m.get("handlers")
    if not isinstance(hs, list) or not hs or not isinstance(hs[0], dict):
        return errs + ["mailto needs a first handler: Firefox ignores useHelperApp without one"]
    u = hs[0].get("uriTemplate", "")
    t = urllib.parse.urlsplit(u)
    errs += need(t.scheme == "https", "the mailto handler must be an https address (Firefox drops any other), got %r" % u)
    errs += need("%s" in t.path or "%s" in t.query, "the mailto handler address must contain %s")
    errs += need(t.hostname == "mail.google.com", "the mailto handler should be Gmail, got %r" % t.hostname)
    errs += need("path" not in hs[0], "the mailto handler must not be a program on the computer")
    return errs


# ── the page ─────────────────────────────────────────────────────────

def check_size(doc, page, raw):
    return need(len(raw) < 50 * 1024, "the start page is %d bytes; the limit is 50 KB" % len(raw))


def check_offline(doc, page, raw):
    errs = []
    for tag, a in page.tags:
        if tag in ("script", "link", "img", "iframe", "frame", "embed", "object", "audio", "video", "source", "base"):
            errs.append("<%s> loads something or runs code; the page must work with nothing else" % tag)
        if tag == "meta" and "refresh" in a.get("http-equiv", "").lower():
            errs.append("<meta refresh> sends her somewhere by itself")
        for k in ("src", "srcset", "poster", "data", "ping"):
            if k in a:
                errs.append("<%s %s=...> loads something" % (tag, k))
        if any(k.startswith("on") for k in a):
            errs.append("<%s> has an event handler (script)" % tag)
    css = "\n".join(page.style)
    if re.search(r"@import|url\(", css, re.I):
        errs.append("the style loads something (@import or url())")
    return errs


def check_addresses_https(doc, page, raw):
    errs = []
    for tag, a in page.tags:
        for k in ("href", "action"):
            if k in a and tag in ("a", "form") and urllib.parse.urlsplit(a[k]).scheme != "https":
                errs.append("<%s %s=%r> is not an https address" % (tag, k, a[k]))
    return errs


def check_buttons(doc, page, raw):
    labels = re.findall(r'<a class="go"[^>]*href="(https://[^"]+)"[^>]*><b>([^<]+)</b>', raw)
    go = {name: href for href, name in labels}
    errs = ["no big button for %r" % b for b in BUTTONS if b not in go]
    errs += ["a button the test does not know: %r" % b for b in go if b not in BUTTONS]
    q = [(t, a) for t, a in page.tags if t == "input" and a.get("name") == "q"]
    f = [(t, a) for t, a in page.tags if t == "form" and a.get("method", "get").lower() == "get"]
    errs += need(bool(q) and bool(f), "no search box (a GET form with an input called q)")
    errs += need(any(t == "button" and a.get("type") == "submit" for t, a in page.tags), "the search box has no Search button")
    return errs


def px(css, selector, prop):
    m = re.search(r"(?:^|\})\s*%s\s*\{([^}]*)\}" % re.escape(selector), css)
    v = re.search(r"(?:^|[;\s])%s\s*:\s*(\d+)px" % prop, m.group(1)) if m else None
    return int(v.group(1)) if v else None


def check_big(doc, page, raw):
    css = "\n".join(page.style)
    errs = []
    h = px(css, "a.go", "min-height")
    if h is None or h < 88:
        errs.append("a.go min-height is %r; a button must be at least 88px tall (docs/EASY.md rule 4 asks 44)" % h)
    for sel, lo in (("body", 24), ("a.go b", 32), ("input", 28), ("button", 28)):
        s = px(css, sel, "font-size")
        if s is None or s < lo:
            errs.append("%s font-size is %r; at least %dpx" % (sel, s, lo))
    for s in re.findall(r"font-size\s*:\s*(\d+)px", css):
        if int(s) < 24:
            errs.append("a font-size of %spx; nothing on this page is under 24px" % s)
    for sel in ("input", "button"):
        h = px(css, sel, "min-height")
        if h is None or h < 64:
            errs.append("%s min-height is %r; at least 64px" % (sel, h))
    return errs


def check_words(doc, page, raw):
    return ["the page says %r (docs/EASY.md rule 2)" % m.group(0) for m in JARGON.finditer(" ".join(page.text))]


CHECKS = [
    # name, function, mutation of (policy doc, page text) that must turn it red
    ("homepage is the start page, locked, used at start-up", check_homepage,
     lambda d, t: d["policies"].pop("Homepage")),
    ("the start page file is in the image", check_page_exists, "nofile"),
    ("no tour, privacy notice, data bar, default question or sign-in offer", check_first_run,
     lambda d, t: d["policies"].update({"DisableFirefoxAccounts": False})),
    ("mailto opens Gmail, asks nothing, cannot loop", check_mailto,
     lambda d, t: d["policies"]["Handlers"]["schemes"]["mailto"].update({"action": "useSystemDefault"})),
    ("mailto handler is an https address (Firefox drops any other)", check_mailto,
     lambda d, t: d["policies"]["Handlers"]["schemes"]["mailto"]["handlers"][0].update(
         {"uriTemplate": "file:///usr/share/auros/start/mail.html?%s"})),
    ("under 50 KB", check_size, lambda d, t: ("page", t + "<!-- " + "x" * 51200 + " -->")),
    ("loads nothing, runs nothing", check_offline,
     lambda d, t: ("page", t.replace("</head>", '<link rel="stylesheet" href="https://x.example/a.css"></head>'))),
    ("only https addresses behind the buttons", check_addresses_https,
     lambda d, t: ("page", t.replace("https://www.youtube.com/", "http://www.youtube.com/"))),
    ("a button for each thing, and a search box", check_buttons,
     lambda d, t: ("page", t.replace("<b>Weather</b>", "<b>Forecast</b>"))),
    ("very large buttons and type", check_big,
     lambda d, t: ("page", t.replace("min-height: 112px", "min-height: 40px"))),
    ("in her words", check_words,
     lambda d, t: ("page", t.replace("Email from Google", "Sign in with your credentials"))),
]


def run(doc, text):
    page = Page(text)
    out = []
    for name, fn, _ in CHECKS:
        out.append((name, fn(doc, page, text)))
    return out


def main(argv):
    prove = "--prove" in argv
    raw = open(PAGE, encoding="utf-8").read() if os.path.isfile(PAGE) else ""
    with open(POLICY, encoding="utf-8") as f:
        doc = json.loads(f.read(), object_pairs_hook=no_dups)
    bad = 0
    print("start page: %s" % os.path.relpath(PAGE, ROOT))
    for name, errs in run(doc, raw):
        print("    %-70s %s" % (name, "ok" if not errs else "FAIL"))
        for e in errs:
            print("        " + e)
        bad += bool(errs)
    if prove and not bad:
        for name, fn, mut in CHECKS:
            d, t = copy.deepcopy(doc), raw
            if mut == "nofile":
                saved = globals()["PAGE"]
                globals()["PAGE"] = PAGE + ".gone"
                red = bool(fn(d, Page(t), t))
                globals()["PAGE"] = saved
            else:
                r = mut(d, t)
                if isinstance(r, tuple):
                    t = r[1]
                    if t == raw:
                        print("    prove: %-60s MUTATION CHANGED NOTHING" % name)
                        bad += 1
                        continue
                red = bool(fn(d, Page(t), t))
            print("    prove: %-60s %s" % (name, "red, as it must be" if red else "STAYED GREEN"))
            bad += not red
    print("startpage: %s" % ("ok" if not bad else "FAIL (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
