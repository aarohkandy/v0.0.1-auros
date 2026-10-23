#!/usr/bin/env python3
"""selftest.py -- the e2e harness's own checks, no VM needed.

1. Each variant's page pattern (VARIANT_EXPECT in e2e.py) matches both
   titles the wizard can show for the stop the variant is made for: the
   preflight's (src/aurbridge/preflight.c) and the fix's (fix_win.c).
   Rewording either one would otherwise make the variant fail on a good
   installer, or a loosened pattern pass on the wrong page; and no
   pattern matches another variant's titles.
2. Edge's download flyout: the warned item's own "More actions" is
   found, never the flyout header's "More options" (run 39896941765
   pressed that four times and stalled; rectangles from its log).
3. The installer's contract with the driver (src/aurbridge/wizard.c):
   its screen-reader object names the primary button's key "Enter",
   and the primary labels still say what the driver acts on (Install,
   Restart now, Fix these for me, Close).
4. publish.sh's four ways out (the run's release; at the default branch;
   the newest e2e-* release as r<run>-*; none at all), with a fake gh.

Run from anywhere: python3 tools/e2e/selftest.py
"""
import ast, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src", "aurbridge")
STOP = {"smalldisk": "insufficient-space", "bitlocker": "bitlocker-system", "pending": "pending-reboot"}
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def variant_expect():
    tree = ast.parse(open(os.path.join(HERE, "e2e.py")).read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "VARIANT_EXPECT":
            return ast.literal_eval(node.value)
    raise SystemExit("VARIANT_EXPECT not found in e2e.py")


def titles(stop):
    pf = open(os.path.join(SRC, "preflight.c")).read()
    fx = open(os.path.join(SRC, "fix_win.c")).read()
    out = []
    m = re.search(r'add\(r,[^;]*?"%s"[^;]*?PF_BLOCK,\s*"([^"]+)"' % re.escape(stop), pf, re.S)
    out.append(("preflight", m.group(1) if m else None))
    m = re.search(r'\{\s*"%s",\s*FIX_\w+,\s*L"([^"]+)"' % re.escape(stop), fx)
    out.append(("fix", m.group(1) if m else None))
    return out


def test_expect():
    exp = variant_expect()
    check(set(exp) == set(STOP), f"VARIANT_EXPECT covers {sorted(STOP)}")
    all_titles = {v: titles(STOP[v]) for v in STOP}
    for v, want in exp.items():
        for where, t in all_titles[v]:
            check(t is not None, f"{v}: the {where} title of '{STOP[v]}' is in the source")
            if t:
                check(re.search(want["page"], t, re.I) is not None,
                      f"{v}: /{want['page']}/ matches the {where} title '{t}'")
        for other, ts in all_titles.items():
            for where, t in ts:
                if other != v and t:
                    check(re.search(want["page"], t, re.I) is None,
                          f"{v}: /{want['page']}/ does not match {other}'s {where} title")


def test_wizard_contract():
    wz = open(os.path.join(SRC, "wizard.c")).read()
    check(re.search(r'ID_PRIMARY\)\s*\?\s*acc_str\(L"Enter"\)', wz) is not None,
          "wizard: the primary button's screen-reader key is Enter (the driver finds it by that)")
    m = re.search(r"primary_label\(void\)\s*\{(.*?)\n\}", wz, re.S)
    body = m.group(1) if m else ""
    for word, why in (("Install AurOS", "starts the install"), ("Restart now", "ends the wizard"),
                      ("Fix these for me", "counts a fix"), ("Close AurOS Setup", "means it cannot go on")):
        check('L"%s"' % word in body, f"wizard: a primary button says '{word}' ({why})")


def test_stops():
    ns = {}
    tree = ast.parse(open(os.path.join(HERE, "e2e.py")).read())
    keep = [n for n in tree.body if (isinstance(n, ast.FunctionDef) and n.name == "close_verdict") or
            (isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "VARIANT_EXPECT")]
    mod = ast.fix_missing_locations(ast.Module(body=[ast.Import(names=[ast.alias(name="re")])] + keep,
                                               type_ignores=[]))
    exec(compile(mod, "e2e.py", "exec"), ns)
    for stop, variant in (("bitlocker-system", "bitlocker"),):
        for where, t in titles(stop):
            v = ns["close_verdict"](variant, f"Close {t} Nothing was changed")
            check("STOPPED AT ITS STOP" in v and "D13" in v, f"{variant}: Close on '{t}' ({where}) is the right red, naming D13")
    check("STOPPED AT ITS STOP" not in ns["close_verdict"]("bitlocker", "Your computer is too old"),
          "bitlocker: Close on some other page is not the right red")
    check("STOPPED AT ITS STOP" not in ns["close_verdict"]("base", "This drive is encrypted with BitLocker"),
          "base: a BitLocker page on the plain PC is not the right red")


def test_edge_item():
    tree = ast.parse(open(os.path.join(HERE, "e2e.py")).read())
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "inside")
    ns = {}
    exec(compile(ast.Module(body=[fn], type_ignores=[]), "e2e.py", "exec"), ns)
    item = {"rect": [789, 130, 344, 100]}
    check(not ns["inside"]({"rect": [1105, 84, 32, 33]}, item), "edge: the flyout header's More options is not the item's")
    check(ns["inside"]({"rect": [1093, 146, 32, 32]}, item), "edge: a button on the item is the item's")
    check(not ns["inside"]({"rect": [0, 0, 0, 0]}, item), "edge: an element with no rectangle is nobody's")


FAKE_GH = r'''#!/bin/bash
echo "gh $*" >> "$T/calls"
case "$1 $2" in
  "release view") [ -f "$T/rel-$3" ] && exit 0; exit 1 ;;
  "release create")
     tgt=""; for ((i=1;i<=$#;i++)); do [ "${!i}" = --target ] && { j=$((i+1)); tgt=${!j}; }; done
     case "$MODE" in ok) ;; sha403) [ "$tgt" = main ] || { echo "HTTP 403" >&2; exit 1; } ;;
       *) echo "HTTP 403" >&2; exit 1 ;; esac
     touch "$T/rel-$3" ;;
  "api repos/o/r") echo main ;;
  "release list") [ "$MODE" = none ] || echo e2e-111 ;;
  "release upload") echo "$3 ${*:4}" >> "$T/uploads" ;;
esac
exit 0
'''


def test_publish():
    want = {"ok": "e2e-222 e2e-evidence-base.zip", "sha403": "e2e-222 e2e-evidence-base.zip",
            "all403": "e2e-111 r222-e2e-evidence-base.zip", "none": None}
    for mode, upload in want.items():
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(f"{t}/bin"); os.makedirs(f"{t}/out/flow/shots")
            open(f"{t}/bin/gh", "w").write(FAKE_GH); os.chmod(f"{t}/bin/gh", 0o755)
            open(f"{t}/out/flow/timings.md", "w").write("# t\n\n| a | 1 |\n")
            env = dict(os.environ, T=t, MODE=mode, PATH=f"{t}/bin:" + os.environ["PATH"], GH_TOKEN="x",
                       GH_REPO="o/r", GITHUB_RUN_ID="222", GITHUB_SHA="abc", E2E_VARIANT="base",
                       E2E_PUBLISH_STATE=f"{t}/state", GITHUB_STEP_SUMMARY=f"{t}/summary")
            pub = os.path.join(HERE, "publish.sh")
            a = subprocess.run(["bash", pub, "init"], env=env, capture_output=True, text=True)
            b = subprocess.run(["bash", pub, "final", f"{t}/out"], env=env, capture_output=True, text=True)
            ups = open(f"{t}/uploads").read() if os.path.exists(f"{t}/uploads") else ""
            if upload:
                check(ups.startswith(upload), f"publish ({mode}): the evidence goes to '{upload}' (got '{ups.strip()}')")
            else:
                check(ups == "", f"publish ({mode}): nothing is uploaded")
            warned = "::warning::" in a.stdout + a.stderr
            check(warned == (mode != "ok"), f"publish ({mode}): {'warns' if mode != 'ok' else 'no warning'}")
            check(a.returncode == 0 and b.returncode == 0, f"publish ({mode}): never fails the job")


test_expect()
test_wizard_contract()
test_stops()
test_edge_item()
test_publish()
print(f"\n{len(fails)} failed" if fails else "\nall passed")
sys.exit(1 if fails else 0)
