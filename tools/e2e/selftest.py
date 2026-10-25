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
5. The installed first-session producer's positive, negative and bounded-retry
   records, plus its systemd and forge wiring (first-session-probetest.sh).

Run from anywhere: python3 tools/e2e/selftest.py
"""
import ast, inspect, json, os, re, subprocess, sys, tempfile

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
    # The fastpath BitLocker confirmation has one deliberately separate
    # restart action. The driver handles it only through its exact warning
    # contract; every other restart-bearing primary would be unsafe ambiguity.
    allowed_restarts = {"Restart now", "Pause protection and restart"}
    others = [t for t in re.findall(r'L"([^"]*)"', body)
              if re.search("restart", t, re.I) and t not in allowed_restarts]
    check(not others,
          f"wizard: only final Restart now and the exact BitLocker confirmation may say restart: {others}")
    styles = re.findall(r'\{\s*"([a-z0-9-]+)",\s*"([a-z0-9-]+)",\s*L"', wz)
    default = re.search(r"#define STYLE_DEFAULT\s+(\d+)", wz)
    shipped = styles[int(default.group(1))] if styles and default else None
    workflow = open(os.path.join(HERE, "..", "..", ".github", "workflows", "e2e.yml")).read()
    inspect_step = workflow.split("\n      - name: M3 - the program is on AurOS's disk", 1)[-1] \
                           .split("\n      # A failed flow", 1)[0]
    generic = inspect_step.rsplit("\n          else\n", 1)[-1]
    match = re.search(r"e2e\.py inspect.*?--expect-look ([a-z0-9-]+).*?"
                      r"--expect-theme ([a-z0-9-]+)\s+fi", generic, re.S)
    checked = match.groups() if match else None
    check(checked == shipped,
          f"wizard: generic E2E checks the shipped default look/theme {shipped} (got {checked})")
    release = open(os.path.join(HERE, "..", "..", ".github", "workflows", "release.yml")).read()
    fastpath = release.split("\n  e2e-fastpath:\n", 1)[-1].split("\n  # THE SOURCE CODE", 1)[0]
    check("variants: base,bitlocker" in fastpath,
          "release: fast-path runs both the base and BitLocker journeys")


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


def test_bitlocker_pause_confirmation():
    try:
        import e2e
    except ImportError:
        return

    sha = "a" * 64
    check(e2e.bitlocker_pause_lane_error(
              "bitlocker", True, "AurOS-Setup-fastpath.exe", sha) is None,
          "pause warning: only the SHA-pinned fastpath BitLocker lane may drive it")
    for variant, putback, name, digest, why in (
            ("base", True, "AurOS-Setup-fastpath.exe", sha, "base variant"),
            ("bitlocker", True, "AurOS-Setup.exe", sha, "generic installer"),
            ("bitlocker", True, "AurOS-Setup-fastpath.exe", "", "missing hash"),
            ("bitlocker", True, "AurOS-Setup-fastpath.exe", "A" * 64, "noncanonical hash")):
        check(e2e.bitlocker_pause_lane_error(variant, putback, name, digest) is not None,
              f"pause warning: {why} is refused")
    check(e2e.bitlocker_pause_lane_error("base", False, "anything.exe", "") is None,
          "pause warning: ordinary lanes remain unchanged")

    restart = {"type": "Button", "name": "Restart now", "enabled": True,
               "key": "Enter", "rect": [600, 600, 200, 50]}
    check(e2e.bitlocker_restart_control_error(
              True, True, [restart], "Restart now", "Restart now") is None,
          "pause warning: exact named Restart now may open the warning")
    for named, raw, canonical, why in (
            ([], "?", "?", "unknown colour/OCR fallback"),
            ([restart], "R¢start now", "Restart now", "fuzzy Restart now"),
            ([{**restart, "name": "Restart later"}], "Restart later", "Restart later",
             "Restart later"),
            ([restart, dict(restart)], "Restart now", "Restart now", "duplicate named primary")):
        check(e2e.bitlocker_restart_control_error(True, True, named, raw, canonical) is not None,
              f"pause warning: {why} is refused before any click")
    check(e2e.bitlocker_restart_control_error(True, False, [], "?", "?") is None,
          "pause warning: pre-install wizard pages retain their existing driver")

    pane = {"type": "Pane", "name": e2e.BITLOCKER_PAUSE_ACCESSIBLE,
            "rect": [200, 120, 800, 400], "enabled": True}
    primary = {"type": "Button", "name": e2e.BITLOCKER_PAUSE_PRIMARY,
               "rect": [650, 460, 260, 50], "enabled": True, "key": ""}
    secondary = {"type": "Button", "name": e2e.BITLOCKER_PAUSE_SECONDARY,
                 "rect": [400, 460, 180, 50], "enabled": True, "key": ""}
    got_primary, got_secondary = e2e.bitlocker_pause_controls([pane, primary, secondary])
    check(got_primary is primary and got_secondary is secondary,
          "pause warning: exact accessible copy and exact two actions are accepted")
    mutations = (
        ([primary, secondary], "missing warning"),
        ([{**pane, "name": pane["name"].replace("anyone", "someone")}, primary, secondary],
         "changed safety copy"),
        ([pane, secondary], "missing explicit primary"),
        ([pane, {**primary, "name": "Pause protection & restart"}, secondary],
         "fuzzy primary"),
        ([pane, {**primary, "enabled": False}, secondary], "disabled primary"),
        ([pane, primary, dict(primary), secondary], "duplicate primary"),
        ([pane, primary], "missing safe secondary"),
        ([pane, primary, {**secondary, "enabled": False}], "disabled safe secondary"),
        ([pane, primary, {**secondary, "name": "Maybe later"}], "renamed safe secondary"),
        ([pane, primary, secondary,
          {"type": "Button", "name": "Help", "rect": [300, 200, 100, 40], "enabled": True}],
         "extra in-warning action"),
        ([pane, {**primary, "rect": [1050, 460, 200, 50]}, secondary],
         "primary outside warning"),
    )
    for controls, why in mutations:
        try:
            e2e.bitlocker_pause_controls(controls)
            refused = False
        except e2e.Failure:
            refused = True
        check(refused, f"pause warning: {why} is refused without a click")

    class FakeMachine:
        def __init__(self, restart=False):
            self.qmp = type("Qmp", (), {"events": []})()
            self.restart = restart
            self.keys = []
        def key(self, key, label=None):
            self.keys.append((key, label))
            if self.restart:
                self.qmp.events.append((1.0, "RESET", {}))
        def alive(self): return True
        def screendump(self): return object()
        def save(self, img, label): pass
        def click(self, x, y, label=None): self.keys.append(("click", label))

    restart_control = {"type": "Button", "name": "Restart now", "enabled": True,
                       "key": "Enter", "rect": [600, 600, 200, 50]}
    flow = object.__new__(e2e.Flow)
    flow.m = FakeMachine()
    flow.wizard_win = lambda: {"proc": "AurOS-Setup-fastpath", "rect": [0, 0, 1000, 700]}
    flow.wizard_named = lambda win: [restart_control]
    flow.bitlocker_state = lambda why: ("FullyEncrypted", "On", 100.0)
    flow.prove_pause_not_armed(0)
    check(flow.m.keys == [("ret", "BitLocker warning: initially focused Not yet")],
          "pause warning: Enter proves the initial Not yet focus and returns to Restart now")

    unsafe_state = object.__new__(e2e.Flow)
    unsafe_state.m = FakeMachine()
    unsafe_state.wizard_win = flow.wizard_win
    unsafe_state.wizard_named = flow.wizard_named
    unsafe_state.bitlocker_state = lambda why: ("FullyEncrypted", "Off", 100.0)
    try:
        unsafe_state.prove_pause_not_armed(0)
        refused = False
    except e2e.Failure:
        refused = True
    check(refused, "pause warning: Not yet that leaves protection off is RED")

    unsafe = object.__new__(e2e.Flow)
    unsafe.m = FakeMachine(restart=True)
    unsafe.wizard_win = flow.wizard_win
    unsafe.wizard_named = flow.wizard_named
    unsafe.bitlocker_state = flow.bitlocker_state
    try:
        unsafe.prove_pause_not_armed(0)
        refused = False
    except e2e.Failure:
        refused = True
    check(refused, "pause warning: an Enter that restarts is RED before explicit confirmation")

    sequence = object.__new__(e2e.Flow)
    sequence.m = FakeMachine()
    labels, proofs = [], []
    sequence.wait_bitlocker_pause_confirmation = lambda label: (
        labels.append(label) or primary, secondary)
    sequence.prove_pause_not_armed = lambda seen: proofs.append(seen)
    first_restarted = sequence.drive_bitlocker_pause_confirmation(False, 7)
    second_restarted = sequence.drive_bitlocker_pause_confirmation(True, 8)
    check(not first_restarted and second_restarted and proofs == [7] and
          labels == ["bitlocker-pause-warning-not-yet", "bitlocker-pause-warning-confirm"] and
          sequence.m.keys == [("click", "wizard " + e2e.BITLOCKER_PAUSE_PRIMARY)],
          "pause warning: first pass proves Not yet; only the second explicitly clicks pause/restart")


def test_bitlocker_putback_contract():
    try:
        import e2e, types
    except ImportError:
        return
    guid = "{01234567-89ab-cdef-0123-456789abcdef}"
    good = {"volume_status": "FullyEncrypted", "protection_status": "On",
            "encryption_percentage": 100, "protectors": [
                {"type": "Tpm", "id": guid}, {"type": "RecoveryPassword", "id": guid}]}
    parsed = e2e.parse_bitlocker_evidence("AUROS_BITLOCKER_STATE " + json.dumps(good))
    check(e2e.bitlocker_return_verdict(parsed) is None,
          "put-back: sanitized fully encrypted/protected state is green")
    for field, value in (("volume_status", "FullyDecrypted"),
                         ("protection_status", "Off"), ("encryption_percentage", 50)):
        bad = dict(parsed); bad[field] = value
        check(e2e.bitlocker_return_verdict(bad) is not None,
              f"put-back: unsafe {field} is RED on either Windows return")
    changed = json.loads(json.dumps(parsed))
    changed["protectors"][0]["id"] = "{11234567-89ab-cdef-0123-456789abcdef}"
    check(e2e.bitlocker_return_verdict(changed, parsed["protectors"]) is not None,
          "put-back: replaced protector identity is RED on either Windows return")
    secret = dict(good); secret["recovery_password"] = "forbidden"
    try:
        e2e.parse_bitlocker_evidence("AUROS_BITLOCKER_STATE " + json.dumps(secret))
        rejected = False
    except e2e.Failure:
        rejected = True
    check(rejected, "put-back: an unexpected secret-bearing BitLocker field is refused")
    try:
        e2e.parse_bitlocker_evidence("AUROS_BITLOCKER_STATE " + json.dumps({**good, "protectors": []}))
        rejected = False
    except e2e.Failure:
        rejected = True
    check(rejected, "put-back: missing protector identities are refused")

    base = {"c_bytes": 1000, "c_offset": 2048, "partition_sha256": "e" * 64,
            "firmware_sha256": "f" * 64, "hiberboot": 1, "canary_sha256": "a" * 64}
    fixture = {"sha256": "b" * 64}
    final = {"phase": "finished", "status_at": "2026-10-24T22:00:00Z",
             "c_bytes": 1000, "c_offset": 2048, "partition_sha256": "e" * 64,
             "firmware_sha256": "f" * 64, "hiberboot": 1,
             "canary_sha256": "a" * 64, "restored_sha256": "b" * 64,
             "handover_volumes": 0, "putback_tasks": 0}
    check(not e2e.putback_completion_errors(base, fixture, final),
          "put-back: exact completed Windows state is green")
    mutations = (("phase", "error"), ("c_bytes", 999), ("c_offset", 999),
                 ("partition_sha256", "0" * 64), ("firmware_sha256", "1" * 64), ("hiberboot", 0),
                 ("canary_sha256", "c" * 64), ("restored_sha256", "d" * 64),
                 ("handover_volumes", 1), ("putback_tasks", 1))
    for field, value in mutations:
        bad = dict(final); bad[field] = value
        check(e2e.putback_completion_errors(base, fixture, bad),
              f"put-back: mismatched {field} fails closed")
    check(e2e.putback_completion_errors(base, fixture, {}),
          "put-back: missing final state fails closed")
    check(e2e.putback_completion_errors(base, fixture, final, 1800000000),
          "put-back: a stale finished status cannot satisfy a new request")

    flow = e2e.Flow.__new__(e2e.Flow)
    flow.m = types.SimpleNamespace(alive=lambda: True)
    try:
        flow.seed_putback_fixture()
        rejected = False
    except e2e.Failure:
        rejected = True
    check(rejected, "put-back: fixture injection refuses a live VM before touching a disk")

    workflow = open(os.path.join(HERE, "..", "..", ".github", "workflows", "e2e.yml")).read()
    release = open(os.path.join(HERE, "..", "..", ".github", "workflows", "release.yml")).read()
    fastpath = release.split("\n  e2e-fastpath:\n", 1)[-1].split("\n  # THE SOURCE CODE", 1)[0]
    ordinary = release.split("\n  e2e:\n", 1)[-1].split("\n  # The fast-path", 1)[0]
    check("bitlocker_putback: true" in fastpath and "bitlocker_putback" not in ordinary,
          "put-back: only the diagnostic fastpath release lane enables the destructive leg")
    check("name == 'AurOS-Setup-fastpath.exe'" in workflow and
          "^[0-9a-f]{64}$" in workflow and "*,bitlocker,*" in workflow,
          "put-back: workflow requires the exact fastpath name, SHA-256 and BitLocker variant")
    check("UNSAFE-DISK-ATTACHED.txt" in workflow,
          "put-back: failure diagnostics never reopen an overlay whose detach was unproven")
    flow_source = open(os.path.join(HERE, "e2e.py")).read().split("def cmd_flow(a):", 1)[-1]
    check(flow_source.index("f.store()") < flow_source.index("f.complete_putback()"),
          "put-back: store installation and real program launch remain before removal")
    check("manage-bde" not in inspect.getsource(e2e.Flow.bitlocker_evidence) and
          "RecoveryPassword" not in inspect.getsource(e2e.Flow.bitlocker_evidence),
          "put-back: evidence schema excludes raw protector output")
    putback_guest_source = (inspect.getsource(e2e.Flow.prepare_putback_windows) +
                            inspect.getsource(e2e.Flow.putback_final_state))
    check("$c=" not in putback_guest_source and "$c." not in putback_guest_source,
          "put-back: guest scripts preserve the helper's command-envelope variable")
    wait_source = inspect.getsource(e2e.Flow.wait_windows_return)
    check(wait_source.index("self.write_bitlocker_return(evidence)") <
          wait_source.index("if time.time() > deadline"),
          "put-back: a RED first or second protection state is recorded before failure")


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

    original_hub = e2e.Hub
    saved = (e2e.Machine, e2e.Hub, e2e.run, e2e.OVMF_VARS, e2e.AurFlow.wait_text)
    try:
        with tempfile.TemporaryDirectory() as t:
            open(f"{t}/vars", "w").close()
            e2e.Machine, e2e.OVMF_VARS = FakeMachine, f"{t}/vars"
            e2e.Hub = lambda rec: original_hub(rec, port=0)
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
        (e2e.Machine, e2e.Hub, e2e.run, e2e.OVMF_VARS, e2e.AurFlow.wait_text) = saved


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


def test_store_magnifier():
    try:
        import e2e, numpy as np
    except ImportError:
        return
    img = np.zeros((800, 1280, 3), np.uint8); img[:] = (240, 240, 240)
    img[:, 192] = 40                                  # the shell's home panel border
    img[75:715, 243] = 40                             # the store window's frame
    box = (488, 90, 150, 20)                          # where OCR found 'Explore'
    check(e2e.AurFlow.store_magnifier(img, box) == (290, 100), "store: the search button is 47 px inside the window frame, on the tabs' row")
    check(e2e.AurFlow.store_magnifier(img, None) == (290, 103), "store: with no Explore tab read by OCR, the header's middle row is used")
    img[300, 243] = 240
    img[:, 243] = 240
    check(e2e.AurFlow.store_magnifier(img, box) is None, "store: no window frame, no guess")


def test_journal_verdicts():
    try:
        import e2e
    except ImportError:
        return
    fx = lambda n: open(os.path.join(HERE, "fixtures", n)).read()
    bare = e2e.journal_verdicts(fx("journal-bare-boot.txt"))
    check(all(ok for _, ok, _ in bare), "journal: a bare boot of the image is green on every first-session verdict (" + "; ".join(f"{n}={ok}" for n, ok, _ in bare) + ")")
    probe_names = {n for n, _, _ in e2e.first_session_probe_verdicts([])}
    check(probe_names == {n for n, _, _ in bare if n in probe_names},
          "journal: all nine root-observed first-session facts are gating verdicts")
    marker = next(l for l in fx("journal-bare-boot.txt").splitlines()
                  if "auros-first-session-probe" in l)
    for field, value, verdict in (
            ("apparmor", "disabled", "AppArmor enabled with a loaded profile"),
            ("profiles", "0", "AppArmor enabled with a loaded profile"),
            ("route", "missing", "default route present"),
            ("dns", "failed", "DNS resolves a public name"),
            ("run_mode", "0700", "/run mode is 0755"),
            ("saved_bin", "present", "staging saved.bin released"),
            ("root_mode", "ro", "installed root filesystem is writable"),
            ("root_fs", "other", "installed root filesystem is ext4"),
            ("aurstage_mount", "present", "no staging mount remains under /run/aurstage"),
            ("aurscreen", "present", "no aurscreen process remains"),
            ("root_mode", "unknown", "installed root filesystem is writable"),
            ("root_fs", "unknown", "installed root filesystem is ext4"),
            ("aurstage_mount", "unknown", "no staging mount remains under /run/aurstage"),
            ("aurscreen", "unknown", "no aurscreen process remains")):
        bad = re.sub(rf"{field}=[^ ]+", f"{field}={value}", marker)
        got = dict((n, ok) for n, ok, _ in e2e.first_session_probe_verdicts([bad]))
        check(got[verdict] is False, f"journal probe: {field}={value} makes '{verdict}' RED")
    missing = e2e.first_session_probe_verdicts([])
    check(len(missing) == 9 and not any(ok for _, ok, _ in missing),
          "journal probe: missing guest evidence makes all nine facts RED")
    duplicate = e2e.first_session_probe_verdicts([marker, marker])
    check(not any(ok for _, ok, _ in duplicate),
          "journal probe: duplicate guest evidence is ambiguous and RED")
    malformed_records = (
        ("missing field", re.sub(r" dns=ok", "", marker)),
        ("duplicate field", marker + " dns=ok"),
        ("trailing token", marker + " trailing"),
        ("non-ASCII profile count", marker.replace("profiles=14", "profiles=\u0661")),
        ("huge profile count", marker.replace("profiles=14", "profiles=" + "9" * 10000)),
    )
    for reason, record in malformed_records:
        malformed = e2e.first_session_probe_verdicts([record])
        check(not any(ok for _, ok, _ in malformed),
              f"journal probe: {reason} makes the whole record malformed and RED")
    real = {n: ok for n, ok, _ in e2e.journal_verdicts(fx("journal-real-install.txt"))}
    check(real.get("network within 60 s of the desktop") is False, "journal: run 40050260949's install, no DHCP lease: network RED")
    check(real.get("Flathub's catalogue fetched") is False, "journal: ... catalogue never fetched: RED")
    check(real.get("clock synced (chrony)") is False, "journal: ... chrony never synced: RED")
    check(real.get("no unit failed that does not fail on the bare boot") is True, "journal: ... no unit failed before the power button (shutdown noise is not counted)")
    broken = fx("journal-bare-boot.txt") + "\n2026-10-24T02:09:30+00:00 auros systemd[1]: auros-software.service: Failed with result 'exit-code'.\n"
    broken = broken.replace("Power key pressed short.", "X").replace("System is powering down.", "Y")
    check(dict((n, ok) for n, ok, _ in e2e.journal_verdicts(broken))["no unit failed that does not fail on the bare boot"] is False,
          "journal: a unit that fails in the session (not at shutdown) is RED")
    slow = "\n".join(l.replace("T02:09:21", "T02:11:21") if ("dhcp4" in l or "CONNECTED_GLOBAL" in l) else l
                     for l in fx("journal-bare-boot.txt").splitlines())
    check(dict((n, ok) for n, ok, _ in e2e.journal_verdicts(slow))["network within 60 s of the desktop"] is False,
          "journal: a lease two minutes after the desktop is RED (the 60 s rule)")
    check(e2e.journal_verdicts("")[0][1] is False, "journal: no journal at all is RED, never silently green")


def test_personalized_verdicts():
    """Pure contract checks. Real personalized readiness still requires the VM run."""
    try:
        import e2e
    except ImportError:
        return
    rid = "sha256:" + "a" * 64
    expected = (rid, "taskbar", "moss")
    shell_source = open(os.path.join(HERE, "..", "..", "src", "aurshell", "main.c"),
                        encoding="utf-8").read()
    loaded_log = re.search(
        r'if \(theme_load\(&t, conf\) < 0\)\s*fprintf\([^;]+;\s*else\s*'
        r'fprintf\([^;]+aurshell: theme [^;]+theme_str\(&t, "theme_name", "unknown"\)',
        shell_source, re.S)
    check(loaded_log is not None,
          "personalized contract: aurshell reports the theme only after theme_load succeeds")
    theme_dir = os.path.join(HERE, "..", "..", "themes")
    for theme_id, label in e2e.PERSONALIZED_THEME_LABELS.items():
        source = open(os.path.join(theme_dir, theme_id + ".theme"), encoding="utf-8").read()
        check(f'theme_name="{label}"' in source,
              f"personalized contract: {theme_id} maps to its shipped renderer label {label!r}")
    first = "\n".join((
        "2026-10-24T01:00:00+00:00 auros kernel: Command line: quiet",
        f"2026-10-24T01:00:04+00:00 auros auros-firstboot[12]: personalized recipe applied: {rid}; theme=moss; shell=taskbar",
        "2026-10-24T01:00:05+00:00 auros aurshell[44]: aurshell: theme 'Moss'",
        "2026-10-24T01:00:05+00:00 auros aurshell[44]: aurshell: archetype 'taskbar' (Taskbar)",
        "2026-10-24T01:01:00+00:00 auros systemd-logind[1]: Power key pressed",
    ))
    log = f"choices: personalized recipe applied: {rid}\n=== first boot complete ===\n"
    receipt = f"language=en_US.UTF-8\nrecipe_id={rid}\ntheme=moss\nshell=taskbar\n"
    good = e2e.personalized_verdicts(expected, receipt, "moss\n",
                                     "/usr/share/auros/shells/taskbar.shell", True, log, first)
    check(all(ok for _, ok, _ in good),
          "personalized contract: matching powered-off artifacts and first-session records are green")
    extended = e2e.personalized_verdicts(expected, receipt, "moss\n",
                                          "/usr/share/auros/shells/taskbar.shell", True, log,
                                          first.replace("; shell=taskbar", "; shell=taskbar; extra=1"))
    check(not dict((name, ok) for name, ok, _ in extended)[
              "first-session journal has applied recipe and theme"],
          "personalized contract: the combined runtime record must match exactly")
    no_live_theme = e2e.personalized_verdicts(
        expected, receipt, "moss\n", "/usr/share/auros/shells/taskbar.shell", True, log,
        first.replace("aurshell: theme 'Moss'", "aurshell: theme unavailable"))
    check(not dict((name, ok) for name, ok, _ in no_live_theme)["live shell loaded chosen theme"],
          "personalized contract: no live renderer theme record is RED")

    missing = e2e.personalized_verdicts(expected, None, None, None, False, None, None)
    check(len(missing) == len(good) and not any(ok for _, ok, _ in missing),
          "personalized contract: every missing installed artifact or live record is RED")

    wrong = e2e.personalized_verdicts(
        expected, f"recipe_id=sha256:{'b' * 64}\ntheme=ember\nshell=dock\n", "ember\n",
        "/usr/share/auros/shells/dock.shell", True,
        f"choices: personalized recipe applied: sha256:{'b' * 64}\n=== first boot complete ===\n",
        first.replace(rid, "sha256:" + "b" * 64).replace("moss", "ember")
             .replace("taskbar", "dock").replace("theme 'Moss'", "theme 'Ember'"))
    by_name = {name: ok for name, ok, _ in wrong}
    check(not any(by_name[name] for name in (
              "saved recipe id", "saved look", "saved theme", "applied theme", "applied look",
              "personalized recipe applied at first boot",
              "first-session journal has applied recipe and theme", "live shell uses chosen look",
              "live shell loaded chosen theme")),
          "personalized contract: a complete but different recipe cannot satisfy the requested one")

    override = e2e.personalized_verdicts(
        expected, "theme=ember\nshell=dock\n", "ember\n",
        "/usr/share/auros/shells/dock.shell", True, "=== first boot complete ===\n",
        first.replace(f"personalized recipe applied: {rid}; theme=moss; shell=taskbar",
                      "personalized override applied: theme=ember; shell=dock")
             .replace("'taskbar'", "'dock'").replace("theme 'Moss'", "theme 'Ember'"))
    check(not dict((name, ok) for name, ok, _ in override)["saved recipe id"] and
          not dict((name, ok) for name, ok, _ in override)["personalized recipe applied at first boot"] and
          not dict((name, ok) for name, ok, _ in override)["live shell loaded chosen theme"],
          "personalized contract: an installer override with no recipe id is explicitly RED")

    later = first + "\n" + "\n".join((
        "2026-10-24T02:00:00+00:00 auros kernel: Command line: quiet",
        f"2026-10-24T02:00:03+00:00 auros auros-firstboot: personalized recipe applied: {rid}; theme=moss; shell=taskbar",
        "2026-10-24T02:00:04+00:00 auros aurshell: theme 'Moss'",
        "2026-10-24T02:00:05+00:00 auros aurshell: archetype 'taskbar' (Taskbar)",
    ))
    empty_first = later.replace(f"personalized recipe applied: {rid}; theme=moss; shell=taskbar",
                                "personalized recipe absent", 1) \
                       .replace("theme 'Moss'", "theme 'Ember'", 1) \
                       .replace("archetype 'taskbar'", "archetype 'dock'", 1)
    after_reboot = e2e.personalized_verdicts(expected, receipt, "moss\n",
                                              "/usr/share/auros/shells/taskbar.shell", True, log, empty_first)
    after_reboot_by_name = {name: ok for name, ok, _ in after_reboot}
    check(not after_reboot_by_name["first-session journal has applied recipe and theme"] and
          not after_reboot_by_name["live shell uses chosen look"] and
          not after_reboot_by_name["live shell loaded chosen theme"],
          "personalized contract: a later boot cannot replace missing first-session recipe, look or theme evidence")


def test_generic_default_verdicts():
    """The generic installer must persist and run its actual shipped defaults."""
    try:
        import e2e
    except ImportError:
        return
    expected = ("taskbar", "sandstone")
    first = "\n".join((
        "2026-10-24T01:00:00+00:00 auros kernel: Command line: quiet",
        "2026-10-24T01:00:05+00:00 auros aurshell[44]: aurshell: theme 'Light'",
        "2026-10-24T01:00:05+00:00 auros aurshell[44]: aurshell: archetype 'taskbar' (Taskbar)",
        "2026-10-24T01:01:00+00:00 auros systemd-logind[1]: Power key pressed",
    ))
    receipt = "language=en_US.UTF-8\ntheme=sandstone\nshell=taskbar\n"
    log = "firstboot: theme applied: sandstone\n=== first boot complete ===\n"
    good = e2e.generic_default_verdicts(
        expected, receipt, "sandstone\n", "/usr/share/auros/shells/taskbar.shell",
        True, log, first)
    check(all(ok for _, ok, _ in good),
          "generic defaults: matching persisted and first-session state is green")

    wrong = e2e.generic_default_verdicts(
        expected, "theme=ember\nshell=dock\n", "ember\n",
        "/usr/share/auros/shells/dock.shell", True, log,
        first.replace("theme 'Light'", "theme 'Ember'")
             .replace("archetype 'taskbar'", "archetype 'dock'"))
    wrong_by_name = {name: ok for name, ok, _ in wrong}
    check(not any(wrong_by_name[name] for name in (
              "saved default look", "saved default theme", "applied default theme",
              "applied default look", "live shell uses default look",
              "live shell loaded default theme")),
          "generic defaults: a complete but different installed look and theme are RED")

    missing = e2e.generic_default_verdicts(expected, None, None, None, False, None, None)
    check(len(missing) == len(good) and not any(ok for _, ok, _ in missing),
          "generic defaults: missing persisted and live evidence is entirely RED")

    no_render_success = e2e.generic_default_verdicts(
        expected, receipt, "sandstone\n", "/usr/share/auros/shells/taskbar.shell",
        True, "=== first boot complete ===\n", first)
    no_render_by_name = {name: ok for name, ok, _ in no_render_success}
    check(not no_render_by_name["default theme render completed"] and
          all(ok for name, ok in no_render_by_name.items()
              if name != "default theme render completed"),
          "generic defaults: otherwise complete evidence without Aurora's success marker is RED")

    personalized = e2e.generic_default_verdicts(
        expected, receipt + "recipe_id=sha256:" + "a" * 64 + "\n", "sandstone\n",
        "/usr/share/auros/shells/taskbar.shell", True, log, first)
    personalized_by_name = {name: ok for name, ok, _ in personalized}
    check(not personalized_by_name["no personalized recipe id"] and
          all(ok for name, ok in personalized_by_name.items()
              if name != "no personalized recipe id"),
          "generic defaults: a personalized receipt with the same look and theme is RED")


def test_store_frame():
    try:
        import e2e, numpy as np
    except ImportError:
        return
    img = np.zeros((800, 1280, 3), np.uint8); img[:] = (240, 240, 240)
    img[:, 192] = 40
    check(e2e.AurFlow.store_frame(img) is None, "store: no window, only the shell's panel border: no frame")
    img[75:715, 243] = 40
    check(e2e.AurFlow.store_frame(img) == 243, "store: a window's left frame is found at its column")


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


def test_store_result():
    """The query and first result can have exactly the same name."""
    try:
        import e2e
    except ImportError:
        return

    def word(text, x, y, w=28, h=18, line=1):
        return {"text": text, "x": x, "y": y, "w": w, "h": h, "line": line}

    # Run 40138682256, screenshot 0045: the old full-screen lookup clicked
    # (343,162), the Search entry, instead of the catalogue title near y=254.
    query = word("VLC", 330, 153, line=1)
    result = word("VLC", 371, 248, line=2)
    words = [query, result,
             word("VLC", 371, 272, line=3), word("media", 405, 272, line=3)]
    check(e2e.AurFlow.store_result(words, 800, "VLC", "VLC") == (371, 248, 28, 18),
          "store result: duplicate query text selects the real catalogue row")
    check(e2e.AurFlow.store_result([query], 800, "VLC", "VLC") is None,
          "store result: query text alone is not a catalogue result")
    duplicate = word("VLC", 371, 348, line=4)
    check(e2e.AurFlow.store_result([result, duplicate], 800, "VLC", "VLC") is None,
          "store result: a missing query cannot make a result's identity ambiguous")
    prefix = word("VLCstream", 371, 248, w=80, line=2)
    prefix_query = word("VLCstream", 330, 153, w=80, line=1)
    description = [word("VLC", 371, 248, line=2),
                   word("media", 405, 248, w=45, line=2)]
    check(e2e.AurFlow.store_result([query, prefix], 800, "VLC", "VLC") is None,
          "store result: a prefixed app name is not the requested result")
    check(e2e.AurFlow.store_result([prefix_query, result], 800, "VLC", "VLC") is None,
          "store result: a prefixed query cannot anchor a result click")
    check(e2e.AurFlow.store_result([query] + description, 800, "VLC", "VLC") is None,
          "store result: description text containing the query is not a title")
    check(e2e.AurFlow.store_result([query, result, duplicate], 800, "VLC", "VLC") is None,
          "store result: duplicate exact result rows fail closed")


def test_program_launch_proof():
    """Store listing and preview text are not proof that the program launched."""
    try:
        import e2e
    except ImportError:
        return

    def word(text, x, y, w=80, h=20, line=1):
        return {"text": text, "x": x, "y": y, "w": w, "h": h, "line": line}

    fixtures = os.path.join(HERE, "fixtures")
    false_base = json.load(open(os.path.join(fixtures, "program-open-false-base.json")))
    false_fast = json.load(open(os.path.join(fixtures, "program-open-false-fastpath.json")))
    controls = ["Media", "Playback", "Audio", "Video", "Subtitle", "Tools", "View", "Help"]
    for label, fixture in (("base error-toast", false_base), ("fastpath no-toast", false_fast)):
        check(e2e.AurFlow.program_window_identity(
                  fixture["words"], fixture["screen_h"], "VLC") is None,
              f"launch: run 40131473307 {label} Software preview is not a mapped VLC app")
        check(len(e2e.AurFlow.program_window_controls(fixture["words"], controls)) >= 2,
              f"launch: run 40131473307 {label} contains misleading preview controls")
    check(e2e.AurFlow.program_launch_error(false_base["words"]) ==
          "Sorry, something went wrong",
          "launch: run 40131473307's real Software error toast is RED")

    preview = false_fast["words"]

    actual_controls = [word("Media", 300, 135, line=5), word("Playback", 360, 135, line=5)]
    task = preview + [word("VLC", 350, 688, line=2)] + actual_controls
    check(e2e.AurFlow.program_window_identity(task, 800, "VLC")[0] == "window task" and
          e2e.AurFlow.program_window_proven(task, 800, "VLC", controls) is None,
          "launch: a mapped shell task must be focused before its controls can prove VLC")
    forged_title = preview + [word("VLC", 520, 95, line=3)]
    check(e2e.AurFlow.program_window_proven(forged_title, 800, "VLC", controls) is None,
          "launch: a VLC title cannot borrow controls from Software's lower preview")
    title = preview + [word("VLC", 520, 95, line=3)] + actual_controls
    check(e2e.AurFlow.program_window_proven(title, 800, "VLC", controls) is not None,
          "launch: a top-level VLC title plus real controls prove a visible mapped app window")
    identity_only = preview + [word("VLC", 350, 688, line=2)]
    check(e2e.AurFlow.program_window_identity(identity_only, 800, "VLC") is not None and
          len(e2e.AurFlow.program_window_controls(identity_only, controls)) >= 2 and
          e2e.AurFlow.program_window_proven(identity_only, 800, "VLC", controls) is None,
          "launch: a task plus Software's preview controls is not enough")

    error_words = [item for item in false_base["words"] if item.get("line") == [3, 1, 1]]
    failed = task + error_words
    check(e2e.AurFlow.program_launch_error(failed) == "Sorry, something went wrong",
          "launch: the real Software error toast is RED even if app identity also appears")
    privacy = [word(text, 250 + offset * 85, 400, line=6)
               for offset, text in enumerate(("Privacy", "and", "Network", "Access", "Policy"))]
    check(e2e.AurFlow.program_launch_error(privacy) is None and
          e2e.AurFlow.program_privacy_step(privacy, 800, "VLC") == (None, None),
          "launch: an unattributed privacy dialog is neither an app nor a store launch error")
    privacy_step = privacy + [word("VLC", 350, 688, line=2),
                              word("Continue", 520, 520, line=7)]
    privacy_identity, privacy_continue = e2e.AurFlow.program_privacy_step(
        privacy_step, 800, "VLC")
    check(privacy_identity[0] == "window task" and privacy_continue is not None and
          e2e.AurFlow.program_window_proven(privacy_step, 800, "VLC", controls) is None,
          "launch: attributed privacy Continue is a transition, never final app proof")
    wrong_continue = privacy + [word("VLC", 350, 688, line=2),
                                word("Continued", 520, 520, line=7),
                                word("Continue", 900, 520, line=8)]
    check(e2e.AurFlow.program_privacy_step(wrong_continue, 800, "VLC")[1] is None,
          "launch: a prefix or unrelated Continue cannot satisfy VLC's modal")


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
    out = ["AUROS_BITLOCKER_STATE " + json.dumps({
        "volume_status": "FullyEncrypted", "protection_status": "Off",
        "encryption_percentage": 100,
        "protectors": [{"type": "Tpm", "id": "{01234567-89ab-cdef-0123-456789abcdef}"}]})]
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


def test_first_session_probe():
    script = os.path.join(HERE, "first-session-probetest.sh")
    r = subprocess.run(["sh", script], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
    check(r.returncode == 0,
          "first session: the journal producer reports real state, stops retrying, and is enabled")


def test_image_integrity_step():
    script = os.path.join(HERE, "image-integrity-test.sh")
    r = subprocess.run(["bash", script], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
    check(r.returncode == 0,
          "image download: pieces and reconstructed image are verified by the real workflow step")


test_expect()
test_wizard_contract()
test_stops()
test_bitlocker_verdict()
test_bitlocker_pause_confirmation()
test_bitlocker_putback_contract()
test_timings_summary()
test_windows_dry()
test_aur_dry()
test_label_is()
test_canon_label()
test_store_magnifier()
test_journal_verdicts()
test_personalized_verdicts()
test_generic_default_verdicts()
test_store_frame()
test_store_content()
test_store_result()
test_program_launch_proof()
test_label_contrast()
test_store_button_colours()
test_flow_helpers()
test_measure()
test_edge_item()
test_publish()
test_first_session_probe()
test_image_integrity_step()
print(f"\n{len(fails)} failed" if fails else "\nall passed")
sys.exit(1 if fails else 0)
