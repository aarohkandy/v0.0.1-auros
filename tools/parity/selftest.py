#!/usr/bin/env python3
"""selftest.py -- what tools/parity can prove about itself and about the files it is there to protect, with no VM.

    python3 tools/parity/selftest.py [--gaps FILE]

--gaps FILE stands in for known-gaps.txt (a red proof: run it with the file as it was before a change).
Standard library only; runs on a Mac.

1. A healthy night (every check passes, as in nightly 41817427536) must exit 0 with the shipped known-gaps.txt.
   A gap that passes fails the run (run.py), so a stale line there is a red nightly, and an email to the owner.
   So must a night on the newest EXISTING image, which lacks this branch's changes: the checks added with them fail
   there, and only the ~pending lines of known-gaps.txt keep that night green. A failure that is not listed, and a
   plain gap that passes, must still be red.
2.The files the `types` and `print` checks stand on: the programs in the profile, the LibreOffice settings, the
   two lines of build/forge. The checks prove them on the built image; this is what makes a deleted line loud
   before a build is spent on it.
"""
import os
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import run  # noqa: E402

fails = []


def check(name, ok, why=""):
    print("  %-72s %s" % (name, "ok" if ok else "FAIL"))
    if not ok:
        fails.append(name)
        if why:
            print("      " + why)


# ── 1. a healthy night ────────────────────────────────────────────────
TYPES = ["pictures", "pdf", "letters", "tables", "notes", "music", "zip", "installer", "folders", "web", "solitaire"]
HEALTHY = (["desktop.welcome", "call.visible", "call.webrtc", "film.dash", "film.dash-visible", "film.h264",
            "film.h264-720p", "film.h264-720p-visible", "film.h264-visible", "print.discover", "print.pdf",
            "print.drivers", "office.opener", "office.save", "office.types", "office.visible", "pdfform.last-box",
            "pdfform.saved", "pdfform.visible", "firefox.firstrun"] + ["types." + t for t in TYPES])


class Nothing:
    path = mnt = ""

    def __init__(self, *a, **k):
        pass

    def __getattr__(self, name):
        return lambda *a, **k: None


def night(gaps_text, failing=()):
    """run.main() on a night where the checks in `failing` fail and the rest of HEALTHY pass; its exit code."""
    tmp = tempfile.mkdtemp(prefix="parity-selftest.")
    try:
        open(os.path.join(tmp, "known-gaps.txt"), "w").write(gaps_text)
        results = {"desktop": {"status": "pass", "detail": "up"}}
        results.update({n: {"status": "fail" if n in failing else "pass", "detail": "fine"} for n in HEALTHY})
        saved = (run.HERE, run.Disk, run.Qemu, run.inject, run.drive, sys.argv, sys.stdout)
        run.HERE, run.Disk, run.Qemu, run.inject = tmp, Nothing, Nothing, lambda *a, **k: None
        run.drive = lambda *a, **k: (results, [], True, 1)
        sys.argv = ["run.py", "--image", "none", "--out", os.path.join(tmp, "out"), "--work", tmp]
        sys.stdout = open(os.devnull, "w")
        try:
            run.main()
            return 0
        except SystemExit as e:
            return e.code
        finally:
            sys.stdout.close()
            run.HERE, run.Disk, run.Qemu, run.inject, run.drive, sys.argv, sys.stdout = saved
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


gaps = sys.argv[sys.argv.index("--gaps") + 1] if "--gaps" in sys.argv else os.path.join(HERE, "known-gaps.txt")
print("a healthy night against %s" % os.path.relpath(gaps, ROOT))
shipped = open(gaps).read()
rc = night(shipped)
check("every check passes: the run exits 0 (no plain gap that passes is left in known-gaps.txt)", rc == 0, "exit code %r" % rc)
pending = [ln.split()[0][1:] for ln in shipped.splitlines() if ln.startswith("~")]
check("known-gaps.txt has pending lines (the newest existing image predates this branch)", len(pending) > 0)
check("an older image: the checks added here fail, the run still exits 0", night(shipped, pending) == 0)
check("  ... but exits 1 with no pending lines (red proof: they are what keep it green)",
      night("", pending) == 1)
check("  ... and exits 1 when a check that is not listed fails too", night(shipped, pending + ["print.discover"]) == 1)
check("  ... and a plain gap that passes is still red", night("types.pdf  x\n") == 1)
check("  ... and a pending line that passes is not", night("~types.pdf  x\n") == 0)
check("the types check is one of the default checks", "types" in run.ALL)
check("every sub-check types.sh reports is one this test knows",
      sorted(re.findall(r"^types (\w+) ", open(os.path.join(HERE, "guest/checks/types.sh")).read(), re.M) + ["solitaire"])
      == sorted(TYPES))


# ── 2. what the checks stand on ───────────────────────────────────────
def read(rel):
    return open(os.path.join(ROOT, rel)).read()


print("the files the checks stand on")
profile = {w for ln in read("profiles/desktop.profile").splitlines() if not ln.lstrip().startswith("#")
           for w in re.split(r'[\s="]+', ln) if w}
for pkg, why in (("printer-driver-escpr", "Epson drivers"),
                 ("printer-driver-brlaser", "Brother drivers"), ("printer-driver-hpcups", "HP drivers"),
                 ("printer-driver-gutenprint", "Canon and Epson photo drivers")):
    check("the profile installs %s (%s)" % (pkg, why), pkg in profile)

forge = read("build/forge")
code = "\n".join(ln for ln in forge.splitlines() if not ln.lstrip().startswith("#"))    # a commented-out line is no line
check("build/forge indexes /usr/local/share/applications, where the renamed launchers live",
      re.search(r'^\s*inchroot "update-desktop-database /usr/local/share/applications\b', code, re.M) is not None)
# (the table of renamed programs, Solitaire's row too, is pinned by tools/renametest.sh; aisleriot by tools/everydaytest.sh)

xcu = os.path.join(ROOT, "rootfs/etc/libreoffice/registry/data/auros.xcu")
want = {("/org.openoffice.Office.Common/Misc", "ShowTipOfTheDay"): "false",
        ("/org.openoffice.Office.Common/Save/Document", "WarnAlienFormat"): "false",
        ("/org.openoffice.Setup/Office/Factories/com.sun.star.text.TextDocument", "ooSetupFactoryDefaultFilter"):
            "MS Word 2007 XML",
        ("/org.openoffice.Setup/Office/Factories/com.sun.star.sheet.SpreadsheetDocument", "ooSetupFactoryDefaultFilter"):
            "Calc MS Excel 2007 XML"}
got = {}
if os.path.exists(xcu):
    oor = "{http://openoffice.org/2001/registry}"
    for item in ET.parse(xcu).getroot().iter("item"):
        for prop in item.iter("prop"):
            got[(item.get(oor + "path"), prop.get(oor + "name"))] = prop.findtext("value")
for (path, prop), val in want.items():
    check("Letters and Spreadsheets: %s %s = %s" % (path.rsplit("/", 1)[-1], prop, val), got.get((path, prop)) == val,
          "rootfs/etc/libreoffice/registry/data/auros.xcu says %r" % got.get((path, prop)))

print()
print("%d failed" % len(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
