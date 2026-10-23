#!/usr/bin/env python3
"""firefoxlive.py -- the browser's scam protection, on the REAL Firefox.

firefoxpolicy.py reads rootfs/etc/firefox/policies/policies.json against
Firefox's schemas. This starts Mozilla's own Firefox (the apt package
the image carries) on a fresh profile with that exact file and asks the
browser what it did with it:

  - every policy in the file is ACTIVE (Firefox silently drops one it
    cannot parse), and every preference the file locks is locked, to the
    value the file gives;
  - the Terms-of-Use window is switched off: the window that says "By
    continuing, you agree ... Firefox sends diagnostic and interaction
    data to Mozilla" and covered her first page on Firefox 158 although
    SkipOnboarding was set. SkipTermsOfUse sets
    termsofuse.acceptedVersion above termsofuse.minimumVersion (which is
    what is checked here); locking the preference
    datareporting.policy.dataSubmissionPolicyBypassNotification hides the
    window too (bisected on the real browser, tools/scamtest/firefoxshots.sh);
  - uBlock Origin is installed (force-installed from addons.mozilla.org);
  - developer tools are off for real: with the file as it is, Firefox
    does not even open its Marionette port, which is the only way this
    script can look inside -- so the other checks run on a copy of the
    file with that one policy removed (the only difference);
  - no crash report: the real Firefox is crashed (SIGSEGV on the main
    process) with the environment rootfs/etc/systemd/system/
    aurshell.service.d/60-auros-no-crash-reports.conf gives every
    program the desktop starts, and nothing is written for Mozilla;
    crashed the same way WITHOUT that environment it writes a crash
    dump and starts Mozilla's crash window ("Tell Mozilla about this
    crash", ticked) -- the control that shows the check can see one.

--prove breaks each protection and insists its check goes red.

    python3 -I tools/scamtest/firefoxlive.py [--prove]

Needs firefox (Mozilla's apt repository), root or passwordless sudo
(Firefox reads /etc/firefox/policies), and python3. Headless: no screen.
Exit 77 (skipped) when there is no firefox, unless SCAMTEST_NEED_FIREFOX=1.
"""
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
POLICY = os.path.join(ROOT, "rootfs", "etc", "firefox", "policies", "policies.json")
DROPIN = os.path.join(ROOT, "rootfs", "etc", "systemd", "system", "aurshell.service.d",
                      "60-auros-no-crash-reports.conf")
ETC = "/etc/firefox/policies/policies.json"
UBLOCK = "uBlock0@raymondhill.net"
PORT = 2828

QUERY = r"""
const out = {};
out.version = Services.appinfo.version;
out.status = Services.policies.status;
out.active = Object.keys(Services.policies.getActivePolicies() || {});
const want = arguments[0];
out.prefs = {};
for (const [n, v] of Object.entries(want)) {
  const t = Services.prefs.getPrefType(n);
  let got = "(unset)";
  if (t === 128) got = Services.prefs.getBoolPref(n);
  else if (t === 64) got = Services.prefs.getIntPref(n);
  else if (t === 32) got = Services.prefs.getStringPref(n);
  out.prefs[n] = { value: got, locked: Services.prefs.prefIsLocked(n) };
}
out.tou = [Services.prefs.getIntPref("termsofuse.acceptedVersion", -1),
           Services.prefs.getIntPref("termsofuse.minimumVersion", -1)];
return out;
"""
UBLOCK_QUERY = r"""
const { AddonManager } = ChromeUtils.importESModule("resource://gre/modules/AddonManager.sys.mjs");
return AddonManager.getAddonByID(arguments[0]).then(a => !!a && a.isActive);
"""


def sudo(*cmd):
    pre = [] if os.geteuid() == 0 else ["sudo", "-n"]
    subprocess.run(pre + list(cmd), check=True)


def install_policy(doc):
    tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
    json.dump(doc, tmp, indent=1)
    tmp.close()
    os.chmod(tmp.name, 0o644)
    sudo("mkdir", "-p", os.path.dirname(ETC))
    sudo("cp", tmp.name, ETC)
    os.unlink(tmp.name)


def dropin_env():
    env = {}
    for line in open(DROPIN, encoding="utf-8"):
        m = re.match(r"\s*Environment=(\w+)=(\S*)\s*$", line)
        if m:
            env[m.group(1)] = m.group(2)
    return env


# ── Marionette, the little of it that is needed ─────────────────────
def _recv(s):
    n = b""
    while not n.endswith(b":"):
        c = s.recv(1)
        if not c:
            raise EOFError
        n += c
    n = int(n[:-1])
    d = b""
    while len(d) < n:
        d += s.recv(n - len(d))
    return json.loads(d)


class Marionette:
    def __init__(self, s):
        self.s, self.i = s, 0
        _recv(s)
        self.call("WebDriver:NewSession", {"capabilities": {}})
        self.call("Marionette:SetContext", {"value": "chrome"})

    def call(self, name, params):
        self.i += 1
        b = json.dumps([0, self.i, name, params]).encode()
        self.s.sendall(str(len(b)).encode() + b":" + b)
        while True:
            r = _recv(self.s)
            if isinstance(r, list) and r[0] == 1 and r[1] == self.i:
                if r[2]:
                    raise RuntimeError("%s: %s" % (name, r[2]))
                return r[3]

    def run(self, script, *args):
        r = self.call("WebDriver:ExecuteScript",
                      {"script": script, "args": list(args), "sandbox": "system"})
        return r["value"] if isinstance(r, dict) and "value" in r else r


class Firefox:
    """One run of the real browser: fresh profile, headless, its own HOME."""

    def __init__(self, env=None):
        self.dir = tempfile.mkdtemp(prefix="fflive-")
        self.home = os.path.join(self.dir, "home")
        os.makedirs(self.home)
        os.makedirs(os.path.join(self.dir, "p"))
        e = dict(os.environ, MOZ_HEADLESS="1", HOME=self.home,
                 # containers and CI runners often forbid the user namespaces
                 # the content sandbox wants; nothing here loads a web page.
                 MOZ_DISABLE_CONTENT_SANDBOX="1")
        for k in ("MOZ_CRASHREPORTER_DISABLE", "MOZ_CRASHREPORTER", "DISPLAY"):
            e.pop(k, None)
        e.update(env or {})
        self.log = open(os.path.join(self.dir, "out"), "w")
        self.p = subprocess.Popen(
            ["firefox", "--profile", os.path.join(self.dir, "p"), "--no-remote",
             "--marionette", "--remote-allow-system-access", "about:blank"],
            env=e, stdout=self.log, stderr=subprocess.STDOUT)

    def connect(self, wait):
        end = time.time() + wait
        while time.time() < end and self.p.poll() is None:
            try:
                return Marionette(socket.create_connection(("127.0.0.1", PORT), timeout=60))
            except (OSError, EOFError):
                time.sleep(0.5)
        return None

    def crash(self):
        """SIGSEGV the main process; True if it died of it."""
        self.p.send_signal(signal.SIGSEGV)
        try:
            self.p.wait(30)
        except subprocess.TimeoutExpired:
            return False
        time.sleep(6)           # the crash handler and its client have had their time
        return True

    def dumps(self):
        """Whatever the crash handler left for Mozilla: the dump, its notes,
        LastCrash, the submit log, the crash ping. Any file at all under
        'Crash Reports' counts -- with the crash handler off the folder
        stays empty -- except the helper's own start-up log."""
        # os.walk, not glob: glob's ** skips hidden folders, and Firefox keeps
        # these under ~/.config (a check that cannot see them passes on anything).
        out = []
        for root, dirs, files in os.walk(self.home):
            if "Crash Reports" in root.split(os.sep):
                out += [os.path.join(root, f) for f in files if f != "crash_helper_server.log"]
        return out

    def reporter_started(self):
        r = subprocess.run(["pgrep", "-x", "crashreporter"], capture_output=True)
        return r.returncode == 0

    def stop(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(15)
            except subprocess.TimeoutExpired:
                self.p.kill()
        subprocess.run(["pkill", "-x", "crashreporter"], capture_output=True)
        for _ in range(50):         # a stale crash window must not count for the next run
            if subprocess.run(["pgrep", "-x", "crashreporter"], capture_output=True).returncode:
                break
            time.sleep(0.1)
        shutil.rmtree(self.dir, ignore_errors=True)


def amo_reachable():
    try:
        socket.create_connection((socket.gethostbyname("addons.mozilla.org"), 443), timeout=8).close()
        return True
    except OSError:
        return False


# ── the checks ──────────────────────────────────────────────────────
ALL = ("closed", "probe", "crash", "control")


def without_devtools(doc):
    probe = json.loads(json.dumps(doc))
    probe["policies"].pop("DisableDeveloperTools", None)
    return probe


def run_all(doc, crash_env, real, only=ALL, ublock=True):
    """doc: the policies document under test; real: the file as it ships, which
    is what Firefox is expected to have done with it. [(name, ok, detail)]."""
    rows = []

    def row(name, ok, detail=""):
        rows.append((name, ok, detail))

    # 1. With the file as it is, developer tools are off: Marionette never opens.
    if "closed" in only:
        install_policy(doc)
        f = Firefox()
        m = f.connect(25)
        row("developer tools are off for real (Marionette stays closed)", m is None,
            "" if m is None else "Firefox opened its remote-control port with DisableDeveloperTools in force")
        f.stop()

    # Everything else goes through a copy with that one policy removed.
    install_policy(without_devtools(doc))

    # 2. What Firefox made of the file.
    if "probe" in only:
        f = Firefox()
        m = f.connect(90)
        if m is None:
            row("Firefox starts on a fresh profile with the file", False,
                open(os.path.join(f.dir, "out")).read()[-600:])
            f.stop()
            return rows
        wanted = {n: v["Value"] for n, v in real["policies"].get("Preferences", {}).items()}
        time.sleep(8)               # first-run work
        got = m.run(QUERY, wanted)
        row("Firefox %s reads the file: policies are active" % got["version"], got["status"] == 1,
            "Services.policies.status = %r (0 inactive, 1 active, 2 failed)" % got["status"])
        lost = [n for n in real["policies"] if n != "DisableDeveloperTools" and n not in got["active"]]
        row("every policy in the file is active (none silently dropped)", not lost,
            "dropped by Firefox: %s" % ", ".join(lost))
        bad = []
        for n, spec in real["policies"].get("Preferences", {}).items():
            g = got["prefs"].get(n, {})
            if g.get("value") != spec["Value"] or (spec.get("Status") == "locked" and not g.get("locked")):
                bad.append("%s: %r locked=%r, file says %r locked" % (n, g.get("value"), g.get("locked"), spec["Value"]))
        row("every locked preference is locked, to the file's value", not bad, "; ".join(bad[:4]))
        acc, mn = got["tou"]
        row("the Terms-of-Use window will not show (accepted >= required)", acc >= mn >= 0,
            "termsofuse.acceptedVersion=%r, minimumVersion=%r: Firefox asks while accepted < minimum" % (acc, mn))
        # uBlock Origin arrives from addons.mozilla.org a little after start-up.
        if ublock and amo_reachable():
            ok = False
            for _ in range(45):
                if m.run(UBLOCK_QUERY, UBLOCK):
                    ok = True
                    break
                time.sleep(2)
            row("uBlock Origin is installed and on", ok, "not installed 90 s after start")
        elif ublock:
            row("uBlock Origin is installed and on (addons.mozilla.org unreachable: SKIPPED)", True)
        f.stop()

    # 3. Crash it the way "Close this page" does, with the desktop's environment.
    if "crash" in only:
        f = Firefox(crash_env)
        m = f.connect(90)
        if m is None:
            row("the crash really happened (the main process died of it)", False, "Firefox did not start")
        else:
            time.sleep(4)
            died = f.crash()
            row("the crash really happened (the main process died of it)", died)
            d = f.dumps()
            row("crashed with the desktop's environment: nothing written for Mozilla, no crash window",
                died and not d and not f.reporter_started(),
                "%d file(s) left for Mozilla (crash dump, notes, ping); crash window started: %s" % (len(d), f.reporter_started()))
        f.stop()

    # 4. The control: the same crash without that environment is seen.
    if "control" in only:
        c = Firefox()
        cm = c.connect(90)
        seen = False
        detail = "the control Firefox did not start"
        if cm is not None:
            time.sleep(4)
            died = c.crash()
            d = c.dumps()
            seen = died and bool(d)
            detail = "control crash: died=%s, files=%d (the check could not see a crash report)" % (died, len(d))
        row("the control crash, without that environment, IS seen (a report is prepared)", seen, detail)
        c.stop()
    return rows


def report(rows):
    bad = 0
    for name, ok, detail in rows:
        print("    %-74s %s" % (name, "ok" if ok else "FAIL"))
        if not ok:
            bad += 1
            if detail:
                print("        " + detail.replace("\n", "\n        "))
    return bad


def main(argv):
    if not shutil.which("firefox"):
        if os.environ.get("SCAMTEST_NEED_FIREFOX") == "1":
            print("firefoxlive: FAIL (no firefox, and this run needs one)")
            return 1
        print("firefoxlive: SKIPPED (no firefox here; CI runs it)")
        return 77
    doc = json.load(open(POLICY, encoding="utf-8"))
    env = dropin_env()
    if env.get("MOZ_CRASHREPORTER_DISABLE") != "1":
        print("    the desktop's environment disables the crash reporter     FAIL\n"
              "        %s does not set MOZ_CRASHREPORTER_DISABLE=1" % os.path.relpath(DROPIN, ROOT))
    print("the real Firefox, with %s" % os.path.relpath(POLICY, ROOT))
    try:
        bad = report(run_all(doc, env, doc))
        if "--prove" in argv:
            print("\n  each protection, broken on purpose, must turn a live check red")
            muts = [
                ("the Terms-of-Use window comes back", "the Terms-of-Use window will not show",
                 lambda d: d["policies"].pop("SkipTermsOfUse"), None, ("probe",)),
                ("a policy is misspelt (Firefox drops it)", "every policy in the file is active",
                 lambda d: d["policies"].update({"HttpsOnlyMod": d["policies"].pop("HttpsOnlyMode")}),
                 None, ("probe",)),
                ("a preference is not locked", "every locked preference is locked",
                 lambda d: d["policies"]["Preferences"]["dom.disable_beforeunload"].update({"Status": "default"}),
                 None, ("probe",)),
                ("developer tools are allowed", "developer tools are off for real",
                 lambda d: d["policies"].pop("DisableDeveloperTools"), None, ("closed",)),
                ("the desktop sets no crash environment", "crashed with the desktop's environment",
                 None, {}, ("crash",)),
            ]
            for what, check, fd, fe, only in muts:
                d = json.loads(json.dumps(doc))
                if fd:
                    fd(d)
                rows = run_all(d, env if fe is None else fe, doc, only=only, ublock=False)
                red = any(n.startswith(check) and not ok for n, ok, _ in rows)
                print("    %-74s %s" % ("broken: " + what, "red, ok" if red else "STILL GREEN"))
                bad += 0 if red else 1
    finally:
        install_policy(json.load(open(POLICY, encoding="utf-8")))
    print("firefoxlive: %s" % ("FAIL (%d)" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
