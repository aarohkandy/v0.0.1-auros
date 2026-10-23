#!/usr/bin/env python3
"""guardprove.py -- tools/guardtest.c, and proof that it can fail.

Builds tools/guardtest.c against the real src/aurshell/guard.c and
foot.c and runs it. Then, with --prove, breaks one protection at a time
in a copy of the tree (guard.c, foot.c or main.c), rebuilds, and insists
the test goes red. Needs cc and libdbus-1-dev (pkg-config dbus-1).

    python3 -I tools/scamtest/guardprove.py [--prove]
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

MUTANTS = [
    ("Escape three times does nothing", "src/aurshell/guard.c",
     "open = G.esc[0] && ms - G.esc[0] <= ESC_WINDOW_MS;", "open = 0;"),
    ("Ctrl+Alt+Delete does nothing", "src/aurshell/guard.c",
     "if (code == K_DEL && G.ctrl && G.alt) {", "if (0) {"),
    ("the key that opened it leaks out on release", "src/aurshell/guard.c",
     "if (value == 0 && (G.held[code >> 3] & bit)) {", "if (0) {"),
    ("a kiosk gets it too", "src/aurshell/guard.c",
     "if (value != 1 || c->kiosk) return 0;", "if (value != 1) return 0;"),
    ("the sound stays on", "src/aurshell/guard.c",
     "        power_mute_set(1);\n", "        (void)0;\n"),
    ("the sound never comes back", "src/aurshell/guard.c",
     "if (G.muted_by_us) power_mute_set(0);", ";"),
    ("'Close this page' only asks politely", "src/aurshell/guard.c",
     "c->want_close_win = 2;", "c->want_close_win = 1;"),
    ("a fourth Escape closes it at once", "src/aurshell/guard.c",
     "if (G.last_key_ms - G.opened_ms < ESC_GRACE_MS) return 1;", ";"),
    ("a blind Return acts", "src/aurshell/guard.c",
     "if (G.sel >= 0 && G.sel < n) act(c, id[G.sel]);", "act(c, id[G.sel < 0 ? 0 : G.sel]);"),
    ("her yes is not written down", "src/aurshell/guard.c",
     "if (write_consent() == 0) {", "if (1) {"),
    ("Escape on the install card means yes", "src/aurshell/guard.c",
     "G.mode == GUARD_REMOTE ? G_NO", "G.mode == GUARD_REMOTE ? G_YES"),
    ("any name from the bus is believed", "src/aurshell/guard.c",
     "if (!plain_name(name) || c->kiosk) return;", "if (c->kiosk) return;"),
    ("the headline runs off the edge at her largest text", "src/aurshell/guard.c",
     "if (hf && f->big && shell_text_w(hf, head) > (float)g.cw) hf = f->big;", ";"),
    ("the warning loses its key sentence", "src/aurshell/guard.c",
     'lines[n++] = "Only continue if YOU called someone you know.";', "(void)0;"),
    ("Help has no way out", "src/aurshell/foot.c",
     "if (!c || c->kiosk || c->n_wins <= 0) return 0;\n    float k", "return 0;\n    float k"),
    ("main.c never shows keys to the guard", "src/aurshell/main.c",
     "if (guard_watch_key(&c, ev.code, ev.value, now_ms())) {", "if (0) {"),
    ("main.c asks politely after all", "src/aurshell/main.c",
     "if (force) {\n", "if (0 && force) {\n"),
    ("main.c forgets the touchpad", "src/aurshell/main.c",
     "pad_taken = guard_click(&c, c.mouse_x, c.mouse_y);", "pad_taken = 0;"),
]


def build(tree, out):
    pc = subprocess.run(["pkg-config", "--cflags", "--libs", "dbus-1"],
                        capture_output=True, text=True)
    if pc.returncode != 0:
        print("    pkg-config dbus-1 failed: apt install libdbus-1-dev pkg-config")
        return False
    srcs = (["tools/guardtest.c", "src/aurshell/guard.c", "src/aurshell/foot.c",
             "src/aurshell/draw.c", "src/aurshell/shellcommon.c", "src/aurshell/anim.c"]
            + sorted(os.path.relpath(p, tree) for p in glob.glob(os.path.join(tree, "src/aurshell/layouts/[a-z]*.c")))
            + ["src/common/theme.c", "src/common/font.c"])
    cmd = (["cc", "-O2", "-std=gnu11", "-o", out] + srcs +
           ["-I", "src/aurshell", "-I", "src/common"] + pc.stdout.split() +
           ["-lm", '-DGUARD_CONSENT_DIR="%s.d"' % out])
    r = subprocess.run(cmd, cwd=tree, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[-3000:])
    return r.returncode == 0


def run(tree, exe, quiet):
    r = subprocess.run([exe, os.path.join(tree, "src/aurshell/main.c")], cwd=tree,
                       capture_output=True, text=True, timeout=60)
    if not quiet:
        sys.stdout.write(r.stdout)
    return r.returncode


def main():
    tmp = tempfile.mkdtemp(prefix="guardprove-")
    try:
        exe = os.path.join(tmp, "guardtest")
        if not build(ROOT, exe):
            print("guardprove: FAIL (does not build)")
            return 1
        bad = 1 if run(ROOT, exe, False) != 0 else 0
        if "--prove" in sys.argv:
            print("\n  each protection, broken on purpose, must turn the test red")
            tree = os.path.join(tmp, "tree")
            for what, rel, old, new in MUTANTS:
                shutil.rmtree(tree, ignore_errors=True)
                for d in ("src/aurshell", "src/common", "tools"):
                    shutil.copytree(os.path.join(ROOT, d), os.path.join(tree, d))
                p = os.path.join(tree, rel)
                text = open(p, encoding="utf-8").read()
                if text.count(old) != 1:
                    print("    %-62s MUTATION DID NOT APPLY" % ("broken: " + what))
                    bad += 1
                    continue
                open(p, "w", encoding="utf-8").write(text.replace(old, new))
                mexe = os.path.join(tmp, "mutant")
                if not build(tree, mexe):
                    print("    %-62s DOES NOT BUILD" % ("broken: " + what))
                    bad += 1
                    continue
                red = run(tree, mexe, True) != 0
                print("    %-62s %s" % ("broken: " + what, "red, ok" if red else "STILL GREEN"))
                if not red:
                    bad += 1
        print("guardprove: ok" if not bad else "guardprove: FAIL (%d)" % bad)
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
