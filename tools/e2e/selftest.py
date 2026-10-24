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
import ast, json, os, re, subprocess, sys, tempfile

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
    check(set(exp) == set(STOP) | {"hdd"}, f"VARIANT_EXPECT covers {sorted(STOP)} and hdd")
    wz = open(os.path.join(SRC, "wizard.c")).read()
    check(re.search(exp["hdd"]["page"], wz.replace("\\u2019", "'"), re.I) is not None,
          "hdd: /%s/ is still what the Ready page says of a slow drive (wizard.c)" % exp["hdd"]["page"])
    all_titles = {v: titles(STOP[v]) for v in STOP}
    for v, want in exp.items():
        if v not in STOP:
            continue
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
    # The driver treats a green button whose label has "restart" in it as the
    # final Restart now and returns. No other primary label may say it.
    others = [t for t in re.findall(r'L"([^"]*)"', body) if re.search("restart", t, re.I) and t != "Restart now"]
    check(not others, f"wizard: no other primary button says 'restart' (the driver would stop there): {others}")


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


def test_bitlocker_verdict():
    tree = ast.parse(open(os.path.join(HERE, "e2e.py")).read())
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "bitlocker_verdict")
    ns = {}
    exec(compile(ast.fix_missing_locations(ast.Module(body=[fn], type_ignores=[])), "e2e.py", "exec"), ns)
    v = ns["bitlocker_verdict"]
    check(v("base", "FullyDecrypted", "Off") is None, "bitlocker: the plain PC with no BitLocker is the plain PC")
    check(v("base", "EncryptionInProgress", "Off") is not None, "bitlocker: a plain PC that Windows began encrypting is refused (run 39923720924)")
    check(v("smalldisk", "FullyEncrypted", "On") is not None, "bitlocker: any non-bitlocker PC with BitLocker on is refused")
    check(v("bitlocker", "FullyEncrypted", "On") is None, "bitlocker: the BitLocker PC fully on is the BitLocker PC")
    check(v("bitlocker", "FullyEncrypted", "Off") is not None, "bitlocker: protection off is not a BitLocker PC")
    check(v("bitlocker", "FullyDecrypted", "Off") is not None, "bitlocker: no BitLocker is not a BitLocker PC")
    with open(os.path.join(HERE, "guest", "specialize.cmd")) as f:
        sp = f.read()
    check("PreventDeviceEncryption" in sp, "the plain PC: Windows' automatic device encryption is switched off in specialize.cmd")


def test_timings_summary():
    sys.path.insert(0, HERE)
    import tempfile as tf, vm
    with tf.TemporaryDirectory() as t:
        r = vm.Recorder(t)
        r.phases = [{"name": n, "start": a, "end": b, "result": "ok"} for n, a, b in (
            ("edge: press the website's download button", 100, 110), ("wizard: first page", 150, 160),
            ("wizard: after 'Next'", 160, 400), ("restart: Windows shuts down", 400, 460),
            ("staging: make room", 460, 1060), ("aurOS: open VLC", 1100, 1130))]
        out = r.summary()
        check("| Get AurOS pressed -> Restart pressed (download, Windows' warnings, the wizard) | 5:00 |" in out,
              "timings: Get AurOS to Restart is 300 s")
        check("(the install itself) | 11:00 |" in out, "timings: the restart to 'does it work?' leg is 660 s")
        check("(the whole journey) | 17:10 |" in out, "timings: the whole journey is 1030 s")
        r.phases[-1]["result"] = "FAILED: x"
        check("whole journey" not in r.summary(), "timings: a journey that failed has no total")


def test_windows_dry():
    """cmd_windows end to end against a fake machine: a slip in it once
    cost a 13-minute Windows install (a variable shadowed the machine)."""
    try:
        import e2e, types
    except ImportError as e:
        print(f"skip  cmd_windows dry run (needs numpy and PIL: {e})")
        return
    facts = lambda vol: json.dumps({"bitlocker": f"\r\nVolumeStatus         : {vol}\r\nProtectionStatus     : Off\r\n",
                                    "variant_name": "base"})

    class FakeMachine:
        def __init__(self, *a, **k): self.up = True
        def start(self): pass
        def auto_shots(self, n): pass
        def shot(self, l): pass
        def alive(self): return self.up
        def stop(self, how="powerdown", t=0): self.up = False

    class FakeHub:
        text = ""
        def __init__(self, rec, www=None): pass
        def wait_event(self, name, timeout, since=0.0, alive=None): return FakeHub.text if name == "firstlogon" else "{}"
        def ask(self, op, **kw): return {"windows": []}

    saved = (e2e.Machine, e2e.Hub, e2e.pass_cd_prompt, e2e.run, e2e.time.sleep, e2e.OVMF_VARS)
    try:
        with tempfile.TemporaryDirectory() as t:
            open(f"{t}/vars", "w").close()
            e2e.Machine, e2e.Hub, e2e.pass_cd_prompt = FakeMachine, FakeHub, lambda m: True
            e2e.run, e2e.time.sleep, e2e.OVMF_VARS = (lambda *a, **k: None), (lambda s: None), f"{t}/vars"
            for vol, rc in (("FullyDecrypted", 0), ("EncryptionInProgress", 1)):
                FakeHub.text = facts(vol)
                a = types.SimpleNamespace(out=f"{t}/out-{vol}", vm=f"{t}/vm-{vol}", iso="i", answer="a")
                os.makedirs(a.out)
                check(e2e.cmd_windows(a) == rc, f"cmd_windows: a Windows that says {vol} gives exit {rc}")
    finally:
        (e2e.Machine, e2e.Hub, e2e.pass_cd_prompt, e2e.run, e2e.time.sleep, e2e.OVMF_VARS) = saved


def test_aur_dry():
    """e2e.py aur against a fake machine whose screen says what AurOS says:
    first screen, the question answered, the store entered, its Failure
    reported with a FAILED.txt (it cannot go on without OCR, which is the point:
    what matters here is that nothing before it is a typo)."""
    try:
        import e2e, types
    except ImportError:
        return
    calls, shots = [], []

    class FakeMachine:
        def __init__(self, *a, **k): self.up = True
        def start(self): calls.append("start")
        def auto_shots(self, n): pass
        def shot(self, l): shots.append(l)
        def alive(self): return self.up
        def stop(self, how="powerdown", t=0): self.up = False; calls.append(how)

    saved = (e2e.Machine, e2e.run, e2e.OVMF_VARS, e2e.AurFlow.wait_text)
    try:
        with tempfile.TemporaryDirectory() as t:
            open(f"{t}/vars", "w").close()
            e2e.Machine, e2e.OVMF_VARS = FakeMachine, f"{t}/vars"
            e2e.run = lambda cmd, rec=None, **k: calls.append(" ".join(cmd))
            e2e.AurFlow.wait_text = lambda self, phrases, timeout, every=10, fail=(): (None, None, None)
            a = types.SimpleNamespace(out=f"{t}/out", image="/img/auros.img", work=f"{t}/work", grow_gb=16)
            os.makedirs(a.out)
            rc = e2e.cmd_aur(a)
            check(rc == 1 and os.path.exists(f"{a.out}/FAILED.txt"),
                  "aur: an AurOS that shows no first screen fails with its words, nothing before it is a typo")
            check(any(c.startswith("qemu-img create") and "-b /img/auros.img" in c and c.endswith("win.qcow2 16G") for c in calls),
                  "aur: the image is booted from an overlay larger than itself (its first start grows the root)")
            check("quit" in calls, "aur: the machine is always stopped")
    finally:
        (e2e.Machine, e2e.run, e2e.OVMF_VARS, e2e.AurFlow.wait_text) = saved


def test_label_is():
    try:
        import e2e
    except ImportError:
        return
    for text, words, want in (("Install", ("Install",), True), ("| Instal |", ("Install",), True),
                              ("Install.", ("Install",), True), ("Installed", ("Install",), False),
                              ("Installing", ("Install",), False), ("Open", ("Open", "Launch"), True),
                              ("0pen", ("Open", "Launch"), True), ("Cancel", ("Install",), False), ("", ("Install",), False)):
        check(e2e.label_is(text, *words) is want, f"store: '{text}' {'is' if want else 'is not'} {'/'.join(words)}")


def test_store_button_colours():
    """The store's main button is found by its accent colour (OCR on the whole
    screen missed the word on a gold button, run 40005591638)."""
    try:
        import e2e, vision, numpy as np
    except ImportError:
        return
    for rgb in e2e.STORE_ACCENTS:
        img = np.zeros((800, 1280, 3), np.uint8)
        img[:] = (24, 28, 34)
        img[214:252, 887:1004] = rgb                      # the button
        img[140:160, 300:400] = (200, 200, 200)           # some text-ish clutter
        got = [b for r in e2e.STORE_ACCENTS for b in vision.find_buttons(img, (240, 100, 800, 330), r)]
        check(got == [(887, 214, 117, 38)], f"store: a button of accent {rgb} is found, and nothing else")
    img[214:252, 887:1004] = (24, 28, 34)
    check(not [b for r in e2e.STORE_ACCENTS for b in vision.find_buttons(img, (240, 100, 800, 330), r)],
          "store: no button, nothing found")


def test_canon_label():
    try:
        import e2e
    except ImportError:
        return
    for raw, want in (("R¢start now", "Restart now"), ("J Install AugOs", "Install AurOS"), ("| Install urOS |", "Install AurOS"),
                      ("Checkpagain", "Check again"), ("gt", "Next"), ("| Next |", "Next"), ("Fix thesé for me", "Fix these for me"),
                      ("Close AurOS Setup", "Close AurOS Setup"), ("Restart later", "Restart later")):
        check(e2e.canon_label(raw) == want, f"wizard: the button read as {raw!r} is '{want}'")
    check(e2e.canon_label("Zzzzz qqq") == "Zzzzz qqq", "wizard: a reading that is nothing known stays as read")


def test_store_content():
    try:
        import e2e, numpy as np
    except ImportError:
        return
    blank = np.zeros((800, 1280, 3), np.uint8); blank[:] = (245, 241, 234)
    blank[80:84, 237:851] = 30                         # a window's frame and header line
    check(not e2e.AurFlow.store_has_content(blank), "store: a window with a header and an empty body is EMPTY")
    full = blank.copy()
    for k in range(10):
        full[160 + k * 50:190 + k * 50, 300:700] = (60 + k * 15, 90, 120)        # rows of content
    check(e2e.AurFlow.store_has_content(full), "store: a window with rows of content has content")


def test_label_contrast():
    """The store's Install must be readable (docs/EASY.md 4.5): dark letters on
    dark green, as run 40038619603 had them, fail; white on green passes."""
    try:
        import vision, numpy as np
    except ImportError:
        return
    def button(fill, ink):
        img = np.zeros((200, 400, 3), np.uint8); img[:] = (245, 240, 230)
        img[50:90, 100:220] = fill
        img[62:78, 135:185] = ink                      # the word, as a block of ink
        return vision.label_contrast(img, (100, 50, 120, 40))
    green = (0x0B, 0x5D, 0x4F)
    check(button(green, (255, 255, 255)) >= 7, "contrast: white on #0B5D4F reads 7:1 or better")
    check(button(green, (28, 28, 28)) < 4.5, "contrast: near-black on #0B5D4F is under 4.5 (what run 40038619603 had)")
    check(button((200, 155, 74), (28, 28, 28)) >= 4.5, "contrast: near-black on gold is fine")
    check(button(green, green) == 1.0, "contrast: a button with no letters at all is 1:1")


def test_flow_helpers():
    try:
        import e2e
    except ImportError:
        return
    f = object.__new__(e2e.Flow)
    f._named_diag = False
    logs, asked = [], []
    f.log = logs.append
    win = {"proc": "AurOS-Setup", "rect": [80, 0, 1100, 740], "name": "Install AurOS"}
    els = [{"type": "Button", "name": "Install AurOS", "proc": "AurOS-Setup", "rect": [950, 650, 190, 56],
            "key": "Enter", "enabled": True},
           {"type": "Button", "name": "Close", "proc": "msedge", "rect": [1, 1, 2, 2]}]
    f.elements = lambda match, types, max=400, quiet=False: (asked.append(match), els)[1]
    got = f.wizard_named(win)
    check([e["name"] for e in got] == ["Install AurOS"], "flow: the installer's controls are the installer process's only")
    check(asked[0] == "^AurOS\\-Setup$", "flow: they are asked of that process by name")
    f.elements = lambda match, types, max=400, quiet=False: (asked.append(match), [])[1]
    asked.clear(); f.wizard_named(win); f.wizard_named(win)
    check(len(asked) == 3 and sum("UI Automation sees" in l for l in logs) == 1,
          "flow: unreadable names are diagnosed once (a broad look, logged), not every second")
    out = ["STATE FullyEncrypted|Off|100\nProtection Status: Protection Off"]
    f.guest = lambda script, timeout=120: out[0]
    check(f.bitlocker_state("t") == ("FullyEncrypted", "Off", 100.0), "flow: BitLocker's state is read from Windows' own words")
    out[0] = "Access is denied"
    saved = e2e.VARIANT
    try:
        e2e.VARIANT = "base"
        check(f.bitlocker_state("t")[0] == "unknown", "flow: the plain PC goes on when BitLocker cannot be read")
        e2e.VARIANT = "bitlocker"
        try:
            f.bitlocker_state("t"); ok = False
        except e2e.Failure:
            ok = True
        check(ok, "flow: the bitlocker PC fails when BitLocker cannot be read")
    finally:
        e2e.VARIANT = saved


def test_measure():
    """The staging's measure step against a real volume's ntfsresize output (measuretest.sh)."""
    r = subprocess.run(["sh", os.path.join(HERE, "measuretest.sh")], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if "skipped" in out:
        print("skip  " + out.strip())
        return
    check(r.returncode == 0, "staging: ntfsresize's 1,660 progress lines do not hide its answer (measuretest.sh)")
    if r.returncode != 0:
        print(out)


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
            "all403": "e2e-111 r222-e2e-evidence-base.zip", "none": None,
            # one run id, two installers: the second's files must not overwrite the first's
            "label": "e2e-222 e2e-evidence-base-fastpath.zip"}
    for mode, upload in want.items():
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(f"{t}/bin"); os.makedirs(f"{t}/out/flow/shots")
            open(f"{t}/bin/gh", "w").write(FAKE_GH); os.chmod(f"{t}/bin/gh", 0o755)
            open(f"{t}/out/flow/timings.md", "w").write("# t\n\n| a | 1 |\n")
            env = dict(os.environ, T=t, MODE="ok" if mode == "label" else mode,
                       E2E_LABEL="-fastpath" if mode == "label" else "", PATH=f"{t}/bin:" + os.environ["PATH"], GH_TOKEN="x",
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
            quiet = mode in ("ok", "label")
            check(warned == (not quiet), f"publish ({mode}): {'no warning' if quiet else 'warns'}")
            check(a.returncode == 0 and b.returncode == 0, f"publish ({mode}): never fails the job")


test_expect()
test_wizard_contract()
test_stops()
test_bitlocker_verdict()
test_timings_summary()
test_windows_dry()
test_aur_dry()
test_label_is()
test_canon_label()
test_store_content()
test_label_contrast()
test_store_button_colours()
test_flow_helpers()
test_measure()
test_edge_item()
test_publish()
print(f"\n{len(fails)} failed" if fails else "\nall passed")
sys.exit(1 if fails else 0)
