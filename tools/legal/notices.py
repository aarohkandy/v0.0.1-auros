#!/usr/bin/env python3
"""notices.py -- the notice files, generated from the texts they quote.

    python3 -I tools/legal/notices.py           write the files
    python3 -I tools/legal/notices.py --check   exit 1 if any file is not what it would write

Writes (all committed, so a reviewer reads them and a build embeds them):
  docs/legal/notices/NOTICES.txt                 for AurOS-Setup.exe (resource 200): the written offer
        of source, AurOS's license, third-party notices, the copyright files of the programs
        inside the installer, and the full texts of the GPL, LGPL, MPL, Apache and Unicode licenses
  docs/legal/notices/TERMS.txt                   the Terms as plain text (resource 202): the
        Ready page's "Read the Terms" opens it, so they can be read with no internet
  rootfs/usr/share/doc/auros/THIRD-PARTY-NOTICES the notices AurOS's own binaries owe (MIT and
        similar licenses ask for the notice to travel with the program); the licenses of Ubuntu's
        programs are in /usr/share/doc/<program>/copyright on every AurOS computer
  rootfs/usr/share/doc/auros/LICENSE             AurOS's own license, beside the others

Nothing here is typed twice: the Wayland notices are cut out of the generated protocol files
the compositor is built from, the font license is the one the website serves, the license
texts are Debian's common-licenses byte for byte. `--check` is part of tools/legal/check.py.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIC = "docs/legal/licenses"
NOTE = "Prepared by the AurOS project without a lawyer. Not legal advice."


def rd(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as fh:
        return fh.read()


def rule(title):
    return "\n" + "=" * 70 + "\n" + title + "\n" + "=" * 70 + "\n\n"


def wayland_notices():
    """Each protocol file's own header, verbatim; files with the same header are listed together."""
    gen = os.path.join(ROOT, "src/aurwl/gen")
    by_text = {}
    for n in sorted(os.listdir(gen)):
        if not (n.endswith("-protocol.c") or n.endswith("-protocol.inc")):
            continue
        t = open(os.path.join(gen, n), encoding="utf-8").read()
        m = re.search(r"/\*\n((?: \*.*\n)+?) \*/", t)
        if not m or "Copyright" not in m.group(1):
            raise SystemExit("notices: no copyright header in src/aurwl/gen/%s" % n)
        body = "\n".join(l[3:] if l.startswith(" * ") else l[2:] for l in m.group(1).rstrip("\n").split("\n"))
        proto = n.replace("-protocol.c", "").replace("-protocol.inc", "")
        by_text.setdefault(body, []).append(proto)
    out = []
    for body, names in sorted(by_text.items(), key=lambda kv: kv[1]):
        out.append("The Wayland protocol description%s %s (used by AurOS's window system):\n\n%s\n"
                   % ("s" if len(names) > 1 else "", ", ".join(names), "\n".join("    " + l if l else "" for l in body.split("\n"))))
    return "\n".join(out)


def third_party(for_installer):
    s = []
    s.append("""These are the notices that the programs AurOS carries from other people ask to travel with
the programs. The licenses of the Ubuntu programs inside AurOS are each in
/usr/share/doc/<program>/copyright on an AurOS computer, and the full texts of the common
licenses in /usr/share/common-licenses. Where the source of these programs is: SOURCE-OFFER.
""")
    s.append(rule("AurOS's own programs: MIT license"))
    s.append(rd("LICENSE").rstrip("\n") + "\n")
    s.append(rule("The Wayland protocol files (compiled into AurOS's window system)"))
    s.append(wayland_notices())
    s.append(rule("Atkinson Hyperlegible Next, the typeface of the website, AurOS Setup and AurOS"))
    s.append("The typeface files are licensed under the SIL Open Font License 1.1. Its notice and text, as the\nproject publishes them:\n\n")
    s.append(rd("website/assets/fonts/OFL.txt").rstrip("\n") + "\n")
    s.append(rule("Time zone names (Ferry's windowsZones.tab, from the Unicode CLDR)"))
    s.append("The file src/ferry/data/windowsZones.tab is derived from data of the Unicode Common Locale Data\nRepository (CLDR). Unicode's license:\n\n")
    s.append(rd(LIC + "/Unicode-3.0.txt").rstrip("\n") + "\n")
    if for_installer:
        s.append(rule("AurOS-Setup.exe: the MinGW-w64 runtime"))
        s.append("""AurOS-Setup.exe was built with MinGW-w64 and is statically linked to its runtime. Parts of that
runtime ask for their copyright and license terms to be included when the program is passed on; they
are below, in the form Ubuntu's mingw-w64 packages publish them. The compiler's own runtime
(libgcc, libssp) is under the GNU GPL with the GCC Runtime Library Exception, which lets programs
that link it be passed on under their own license.

""")
        s.append(rd(LIC + "/mingw-w64-copyright.txt").rstrip("\n") + "\n")
        s.append(rule("The small system that runs after the restart: how it is linked"))
        s.append("""Its own programs (init and the screen it draws) are AurOS code under the MIT license, linked
statically to the GNU C library (LGPL 2.1 or later). Anyone may replace that library and link them
again: the source of both, and the commands, are in the source code (docs/RELINK.md) and in the
sources release (see the offer at the top).
""")
    return "".join(s)


def offer_text():
    t = rd("rootfs/usr/share/doc/auros/SOURCE-OFFER")
    return t.rstrip("\n") + "\n"


def notices_txt():
    s = ["AUROS: LICENCES AND NOTICES\n\n" + NOTE + "\n\n"
         "This file is inside AurOS-Setup.exe so that you can read it with no internet. It has, in order:\n"
         "  1. our written offer of the source code\n"
         "  2. the notices of the programs AurOS carries (third-party notices)\n"
         "  3. the copyright files of the programs inside the installer's small system and boot files\n"
         "  4. the full texts of the GNU GPL 2 and 3, the GNU LGPL 2.1, the Mozilla Public License 2.0,\n"
         "     the Apache License 2.0 and the Unicode license\n"]
    s.append(rule("1. THE SOURCE CODE"))
    s.append(offer_text())
    s.append(rule("2. NOTICES OF THE PROGRAMS AUROS CARRIES"))
    s.append(third_party(True))
    s.append(rule("3. COPYRIGHT FILES OF THE PROGRAMS INSIDE AUROS-SETUP.EXE"))
    s.append(rd(LIC + "/component-copyrights.txt").rstrip("\n") + "\n")
    s.append(rule("4. THE FULL TEXTS"))
    for name, title in (("GPL-2.0.txt", "GNU General Public License, version 2"),
                        ("GPL-3.0.txt", "GNU General Public License, version 3"),
                        ("LGPL-2.1.txt", "GNU Lesser General Public License, version 2.1"),
                        ("MPL-2.0.txt", "Mozilla Public License, version 2.0"),
                        ("Apache-2.0.txt", "Apache License, version 2.0")):
        s.append("\n----- %s -----\n\n" % title)
        s.append(rd(LIC + "/" + name).rstrip("\n") + "\n")
    return "".join(s)


def md_to_text(md):
    out = []
    for line in md.split("\n"):
        line = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", line)      # links keep their words
        line = re.sub(r"\*\*([^*]+)\*\*", r"\1", line)
        line = re.sub(r"`([^`]+)`", r"\1", line)
        if line.startswith("# "):
            out.append(line[2:].upper()); out.append("")
        elif line.startswith("## "):
            out.append(""); out.append(line[3:]); out.append("-" * len(line[3:]))
        elif line.startswith("- "):
            out.append("  * " + line[2:])
        else:
            out.append(line)
    t = "\n".join(out)
    return re.sub(r"\n{3,}", "\n\n", t).strip("\n") + "\n"


def files():
    return {
        "docs/legal/notices/NOTICES.txt": notices_txt(),
        "docs/legal/notices/TERMS.txt": md_to_text(rd("docs/legal/TERMS.md")),
        "rootfs/usr/share/doc/auros/THIRD-PARTY-NOTICES": "AUROS: NOTICES OF THE PROGRAMS IT CARRIES\n\n" + NOTE + "\n\n" + third_party(False),
        "rootfs/usr/share/doc/auros/LICENSE": rd("LICENSE"),
    }


def main(argv):
    bad = 0
    for rel, text in files().items():
        p = os.path.join(ROOT, rel)
        have = open(p, encoding="utf-8").read() if os.path.isfile(p) else None
        if "--check" in argv:
            if have != text:
                print("notices: %s is not what tools/legal/notices.py writes (run it)" % rel); bad += 1
        elif have != text:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w", encoding="utf-8").write(text)
            print("notices: wrote %s (%d bytes)" % (rel, len(text.encode())))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
