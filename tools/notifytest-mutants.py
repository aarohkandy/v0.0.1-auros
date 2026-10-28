#!/usr/bin/env python3
"""notifytest-mutants.py -- prove the screensaver checks in tools/notifytest.c can fail.

Breaks ONE thing in a copy of src/aurshell/notify.c (an exact replacement that must match once),
builds notifytest against it and requires a "FAIL" line from the check it is aimed at. A compile
error, a crash or a missing line is not a catch. Needs libdbus-1-dev, dbus-daemon, gdbus.

    python3 tools/notifytest-mutants.py
"""
import os, re, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "aurshell", "notify.c")
# (what is broken, text of the check that must say FAIL, old, new)
MUTANTS = [
    ("the screensaver service answers nothing", "a program can ask for the screen to stay on",
     "if (dbus_message_is_method_call(m, SAVER_NAME, \"Inhibit\")) {", "if (0) {"),
    ("an inhibitor is not counted", "and the shell then counts it as her being there",
     "    int on = sv_n > 0 || sv_poke;", "    int on = sv_poke;"),
    ("anybody can end anybody's inhibitor", "another program cannot end it",
     "if (SV[i].cookie == k && !strcmp(SV[i].who, who))", "if (SV[i].cookie == k)"),
    ("UnInhibit ends nothing", "the program that asked ends it",
     "{ SV[i] = SV[--sv_n]; break; }", "{ break; }"),
    ("the KDE path is not served", "the older KDE path (/ScreenSaver) is served too",
     "dbus_connection_register_object_path(N.conn, \"/ScreenSaver\", &SAVER_VTABLE, NULL)) {", "0) {"),
    ("wrong arguments are believed", "a call with the wrong arguments is refused",
     "if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &app, DBUS_TYPE_STRING, &why, DBUS_TYPE_INVALID)) {", "if (0) {"),
    ("a program that quits keeps the screen on", "a program that quits without ending it lets go",
     "        sv_drop_who(old);", "        ;"),
    ("no limit on how many one program takes", "one program cannot take an unlimited number",
     "mine >= SAVER_EACH", "0"),
]

def main():
    src = open(SRC).read()
    tmp = tempfile.mkdtemp(prefix="notifytest-mut.")
    bad = 0
    cc = ["cc", "-O1", "-std=gnu11", "-w"]
    others = [os.path.join(ROOT, "src/aurshell", f) for f in ("osd.c", "foot.c", "draw.c", "shellcommon.c", "anim.c")]
    others += [os.path.join(ROOT, "src/aurshell/layouts", f) for f in sorted(os.listdir(os.path.join(ROOT, "src/aurshell/layouts"))) if f.endswith(".c")]
    others += [os.path.join(ROOT, "src/common", f) for f in ("theme.c", "font.c")]
    libs = subprocess.run(["pkg-config", "--cflags", "--libs", "dbus-1"], capture_output=True, text=True).stdout.split()
    try:
        for what, check, old, new in MUTANTS:
            if src.count(old) != 1:
                print("STALE  %s (matches %d times)" % (what, src.count(old))); bad += 1; continue
            path = os.path.join(tmp, "notify.c")
            open(path, "w").write(src.replace(old, new))
            exe = os.path.join(tmp, "t")
            b = subprocess.run(cc + ["-o", exe, os.path.join(ROOT, "tools/notifytest.c"), path] + others + libs +
                               ["-I", os.path.join(ROOT, "src/aurshell"), "-I", os.path.join(ROOT, "src/common"), "-lm"],
                               capture_output=True, text=True, cwd=ROOT)
            if b.returncode:
                print("BUILD  %s\n%s" % (what, b.stderr[-500:])); bad += 1; continue
            r = subprocess.run([exe], capture_output=True, text=True, cwd=ROOT, timeout=300)
            if re.search(r"^.*%s.*FAIL\s*$" % re.escape(check), r.stdout, re.M):
                print("caught %-52s by \"%s\"" % (what, check))
            else:
                print("ALIVE  %-52s (\"%s\" did not say FAIL; exit %d)" % (what, check, r.returncode)); bad += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%s: %d mutants, %d not caught" % ("FAIL" if bad else "PASS", len(MUTANTS), bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
