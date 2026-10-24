#!/usr/bin/env python3
"""Is AurOS up? (WP-35.) One small job, every 15 minutes (.github/workflows/monitor.yml).

    python3 -I tools/ops/monitor.py [--deep] [--out status.json] [--issues]
    python3 -I tools/ops/monitor.py --selftest

Checks the four things a person meets, against today's addresses
(tools/repo.sh: GitHub's until AUROS_DOMAIN is set, then the owner's):
  site      the website answers 200
  download  "Get AurOS" delivers a file (a one-byte Range request);
            --deep (hourly) downloads it and compares its sha256 with the
            release's SHA256SUMS and with the site's release.json (the sha256
            release.yml recorded when it made the release the full one; served
            by Pages, so replacing release assets does not change it: SEC-005)
  pieces    every piece of the image, from pieces.txt, answers a Range
            request with 206
  channel   the update channel's InRelease verifies against the committed
            keyring, and its Valid-Until, if it has one, is > 3 days away
  wishes    the Wishes Worker's /v1/health says {"ok":true}
  flathub   Flathub's repository answers (information only: never pages)
A check that is not yet launched must not wake anybody: only the names in
docs/ops/live.txt page. The rest are "pending" and are listed in every run.
--issues opens (or closes) one GitHub issue per live failing check, assigned
to the owner (and the second responder), with the `gh` that is logged in.
Exit 1 if a live check failed. docs/ops/INCIDENTS.md says what to do.
"""
import hashlib, http.client, json, os, re, socket, subprocess, sys, tempfile, time, urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
TIMEOUT = float(os.environ.get("MON_TIMEOUT", "25"))
KEYRING = os.path.join(ROOT, "rootfs/usr/share/keyrings/auros-archive-keyring.asc")
MIN_EXE = 1_000_000  # bytes: anything smaller is not AurOS-Setup.exe
ALL = ["site", "download", "pieces", "channel", "wishes", "flathub", "gate"]


def repo(what):
    return subprocess.run(["sh", os.path.join(ROOT, "tools/repo.sh"), what], check=True, capture_output=True,
                          text=True).stdout.strip()


def addresses(env=os.environ):
    """The URLs to watch. Each can be overridden (a test's local server)."""
    dom = env.get("AUROS_DOMAIN", "").strip()
    get = env.get("MON_GET") or repo("get")
    wishes = env.get("MON_WISHES") or (f"https://wish.{dom}" if dom else env.get("WISHES_URL", ""))
    return {
        "site": env.get("MON_SITE") or repo("site"),
        "get": get,
        "dir": get.rsplit("/", 1)[0],  # the release's files: SHA256SUMS, pieces.txt
        "apt": env.get("MON_APT") or repo("apt"),
        "wishes": wishes,
        "flathub": env.get("MON_FLATHUB", "https://dl.flathub.org/repo/config"),
        "keyring": env.get("MON_KEYRING", KEYRING),
        # The edge Worker's public state (infra/edge): only once the owner has a domain
        "gate": env.get("MON_GATE") or (f"https://get.{dom}" if dom else ""),
    }


def fetch(url, rng=None, body=True, tries=3, method=None, data=None):
    """(status, headers, body): ALWAYS a definite answer, never None and never a raise.
    status 0 means no answer at all (timeout, refused, DNS, a broken reply); body is
    b"" when none was asked for or came. Retries a network error, 5xx and 429, so one
    hiccup pages nobody. A monitor must not crash on the outage it exists to report."""
    last = (0, {}, b"")
    for _ in range(tries):
        try:
            req = urllib.request.Request(url, data=data, method=method,
                                         headers={"User-Agent": "auros-monitor", **({"Range": rng} if rng else {})})
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                return r.status, {k.lower(): v for k, v in r.headers.items()}, (r.read() if body else b"")
        except urllib.error.HTTPError as e:
            last = (e.code, {k.lower(): v for k, v in (e.headers or {}).items()}, b"")
            if e.code < 500 and e.code != 429:
                return last
        except Exception as e:  # URLError, timeout, ssl, http.client, ValueError for a bad URL, ...
            last = (0, {}, str(e).encode()[:200])
        time.sleep(0 if os.environ.get("MON_FAST") else 3)
    return last


def said(s):
    """How an outage reads in a sentence."""
    return str(s) if s else "nothing (no answer)"


def check_site(a, deep):
    s, _, b = fetch(a["site"])
    if s != 200:
        return f"{a['site']} answered {said(s)}"
    if b"AurOS" not in b:
        return f"{a['site']} answered 200 but is not the AurOS page"


def sha_of(url):
    h, n = hashlib.sha256(), 0
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "auros-monitor"}), timeout=TIMEOUT) as r:
        while True:
            c = r.read(1 << 20)
            if not c:
                return h.hexdigest(), n
            h.update(c); n += len(c)


def check_download(a, deep):
    s, h, _ = fetch(a["get"], rng="bytes=0-0", body=False)
    if "x-auros-gate" in h:
        return  # shut on purpose (not open yet, full today, or paused): the gate check says which, and pages if it is a pause
    if s not in (200, 206):
        return f"{a['get']} answered {said(s)}"
    # An empty or truncated file also answers 200: the installer is tens of MB.
    size = (h.get("content-range", "").rsplit("/", 1)[-1] if s == 206 else h.get("content-length", ""))
    if not size.isdigit() or int(size) < MIN_EXE:
        return f"{a['get']} answered {s} but its size is {size or 'unknown'} bytes, not an installer"
    if not deep:
        return
    s, _, sums = fetch(a["dir"] + "/SHA256SUMS")
    if s != 200:
        return f"{a['dir']}/SHA256SUMS answered {said(s)}"
    want = {l.split()[1].lstrip("*"): l.split()[0] for l in sums.decode("utf-8", "replace").splitlines() if len(l.split()) == 2}
    if "AurOS-Setup.exe" not in want:
        return "SHA256SUMS has no AurOS-Setup.exe line"
    # The value that is not a release asset: release.json on the site, written by release.yml's
    # promote path and deployed by Pages. SHA256SUMS lives in the release, so whoever replaces
    # the exe there can replace it too; this cannot be replaced from the release. A site
    # with no release.json yet (no [latest] since this existed) is not an outage.
    ps, _, pb = fetch(a["site"].rstrip("/") + "/release.json")
    pinned = ""
    if ps == 200:
        try:
            pinned = str(json.loads(pb)["sha256"]).lower()
            if not re.fullmatch(r"[0-9a-f]{64}", pinned):
                raise ValueError(pinned)
        except (ValueError, KeyError, TypeError):
            return f"{a['site']}release.json is not {{\"sha256\": <64 hex>}}"
    try:
        got, n = sha_of(a["get"])
    except (urllib.error.URLError, http.client.HTTPException, OSError) as e:
        return f"downloading {a['get']} failed: {e}"
    if got != want["AurOS-Setup.exe"]:
        return f"the downloaded file ({n} bytes) hashes to {got[:16]}..., SHA256SUMS says {want['AurOS-Setup.exe'][:16]}..."
    if pinned and got != pinned:
        return f"the downloaded file ({n} bytes) hashes to {got[:16]}..., the site's release.json (what release.yml recorded) says {pinned[:16]}...: the release was changed after it was made"


def check_pieces(a, deep):
    s, _, b = fetch(a["dir"] + "/pieces.txt")
    if s != 200:
        return f"{a['dir']}/pieces.txt answered {said(s)}"
    lines = b.decode("utf-8", "replace").splitlines()
    bases = [l.split()[1] for l in lines if l.startswith("base ")]
    pieces = [l.split() for l in lines if l.startswith("piece ")]
    if not bases or not pieces:
        return "pieces.txt names no base or no piece"
    bad = []
    for base in bases[:1]:  # the first base is what the installer tries first
        for p in pieces:
            s, h, _ = fetch(base + p[1], rng="bytes=0-0", body=False)
            if s != 206:
                bad.append(f"{p[1]} answered {said(s)} to a Range request ({base})")
    if bad:
        return "; ".join(bad[:3]) + (f" (+{len(bad) - 3} more)" if len(bad) > 3 else "")


def check_channel(a, deep):
    s, _, b = fetch(a["apt"] + "InRelease")
    if s != 200:
        return f"{a['apt']}InRelease answered {said(s)}"
    keyring = a["keyring"]
    with tempfile.TemporaryDirectory() as d:
        os.chmod(d, 0o700)
        open(os.path.join(d, "InRelease"), "wb").write(b)
        g = ["gpg", "--batch", "--homedir", d]
        try:
            subprocess.run(g + ["--import", keyring], check=True, capture_output=True)
            v = subprocess.run(g + ["--status-fd", "1", "--verify", os.path.join(d, "InRelease")], capture_output=True, text=True)
        except FileNotFoundError:
            return "gpg is not installed here, so InRelease could not be verified"
        except subprocess.CalledProcessError as e:
            return f"the committed keyring could not be read: {e.stderr[-120:]!r}"
        if "VALIDSIG" not in v.stdout or v.returncode != 0:
            return "InRelease does not verify against the committed keyring (" + (v.stdout + v.stderr).strip().splitlines()[-1][:100] + ")"
    m = re.search(rb"^Valid-Until: (.+)$", b, re.M)
    if m:
        import email.utils
        until = email.utils.parsedate_to_datetime(m.group(1).decode()).timestamp()
        days = (until - time.time()) / 86400
        if days < 3:
            return f"InRelease Valid-Until is in {days:.1f} days (re-sign it: docs/ops/INCIDENTS.md)"


def check_wishes(a, deep):
    if not a["wishes"]:
        return "no Wishes address (set the repository variable AUROS_WISHES_URL)"
    s, _, b = fetch(a["wishes"].rstrip("/") + "/v1/health")
    if s != 200 or b'"ok":true' not in b.replace(b" ", b""):
        return f"{a['wishes']}/v1/health answered {said(s)}: {b[:80]!r}"
    # The whole service is answering, not just the health route: a stranger (no
    # token) must be refused with 401 before anything else is read. A 5xx here is
    # the service resting or down (docs/CLOUD.md section 5).
    s, _, b = fetch(a["wishes"].rstrip("/") + "/v1/wish", method="POST", data=b"{}", tries=2)
    if s != 401:
        return f"{a['wishes']}/v1/wish without a token answered {said(s)}, not 401: {b[:80]!r}"


def check_flathub(a, deep):
    s, _, _ = fetch(a["flathub"], body=False)
    if s != 200:
        return f"{a['flathub']} answered {said(s)}"


def check_gate(a, deep):
    s, _, b = fetch(a["gate"].rstrip("/") + "/_ops/state")
    try:
        st = json.loads(b) if s == 200 else None
    except ValueError:
        st = None
    if not isinstance(st, dict):
        return f"{a['gate']}/_ops/state answered {said(s)}, not the gate's state"
    if st.get("paused"):
        return f"downloads are PAUSED: {st.get('reason') or 'no reason given'}. Look into it, then: python3 tools/ops/waves.py resume (docs/ops/INCIDENTS.md)"


FUNCS = dict(gate=check_gate, site=check_site, download=check_download, pieces=check_pieces, channel=check_channel,
             wishes=check_wishes, flathub=check_flathub)


def live_names(env=os.environ):
    p = env.get("MON_LIVE_FILE", os.path.join(ROOT, "docs/ops/live.txt"))
    try:
        return {w for l in open(p) for w in l.split("#")[0].split()}
    except OSError:
        return set()


def run(deep=False, env=os.environ):
    a, live, out = addresses(env), live_names(env), []
    for name in (n for n in ALL if n != "gate" or a["gate"]):
        t = time.time()
        try:
            why = FUNCS[name](a, deep)
        except Exception as e:  # a check that crashes is a check that failed
            why = f"the check itself broke: {type(e).__name__}: {e}"
        # flathub is information: never pages
        state = "ok" if not why else ("fail" if name in live and name != "flathub" else "pending" if name != "flathub" else "warn")
        out.append(dict(check=name, state=state, detail=why or "", ms=int((time.time() - t) * 1000)))
    return out


def summary(results):
    ok = not any(r["state"] == "fail" for r in results)
    return {"checked_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "ok": ok, "results": results}


def gh(*args, check=True):
    return subprocess.run(["gh", *args], check=check, capture_output=True, text=True).stdout


def issues(results):
    """One open issue per live failing check, assigned to the owner (and the second responder); closed when it recovers.
    MON_ISSUE_PREFIX ("monitor") names the label and the titles; the drill uses "drill" so it never mixes with a real alert."""
    repo_ = os.environ["GITHUB_REPOSITORY"]
    pre = os.environ.get("MON_ISSUE_PREFIX", "monitor")
    assignees = [os.environ.get("GITHUB_REPOSITORY_OWNER") or repo_.split("/")[0]]
    if os.environ.get("AUROS_SECOND_RESPONDER"):
        assignees.append(os.environ["AUROS_SECOND_RESPONDER"])
    gh("label", "create", pre, "--color", "B60205", "--description", f"{pre}: AurOS is broken" if pre == "monitor" else "a rehearsal", "--force", "-R", repo_, check=False)
    open_ = {i["title"]: i["number"] for i in json.loads(gh("issue", "list", "-R", repo_, "--label", pre, "--state", "open", "--json", "number,title"))}
    run_url = f"{os.environ.get('GITHUB_SERVER_URL', 'https://github.com')}/{repo_}/actions/runs/{os.environ.get('GITHUB_RUN_ID', '')}"
    for r in results:
        title = f"{pre}: {r['check']} is down"
        if r["state"] == "fail" and title not in open_:
            body = (f"{r['detail']}\n\nSeen at {time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime())}: {run_url}\n\n"
                    f"What to do: docs/ops/INCIDENTS.md (find `{r['check']}`). This issue closes itself when the check passes again.")
            args = ["issue", "create", "-R", repo_, "--title", title, "--label", pre, "--body", body]
            for who in assignees:
                args += ["--assignee", who]
            gh(*args)
        elif r["state"] == "ok" and title in open_:
            gh("issue", "close", str(open_[title]), "-R", repo_, "--comment", f"{r['check']} passes again ({run_url}).")


# ── self-test: a local server that can be broken in each way ──────────────────

def selftest():
    import threading, http.server, shutil
    os.environ["MON_FAST"] = "1"  # no waiting between retries: a test, not a night watch
    ok = True
    exe = os.urandom(1_200_000)
    good_sum = hashlib.sha256(exe).hexdigest()
    gpg = shutil.which("gpg")

    def sign(d, text, key_home):
        open(os.path.join(d, "Release"), "w").write(text)
        subprocess.run(["gpg", "--batch", "--homedir", key_home, "--yes", "--clearsign", "-o", os.path.join(d, "InRelease"), os.path.join(d, "Release")],
                       check=True, capture_output=True)
        return open(os.path.join(d, "InRelease"), "rb").read()

    state = {}
    holder = {"inrelease": b""}  # the signed Release served at /apt/InRelease

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def send(self, code, body=b"", extra=()):
            self.send_response(code)
            for k, v in extra:
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def outage(self):
            """True if this path is broken on purpose (state["modes"][path]) and the reply was spoilt."""
            mode = state.get("modes", {}).get(self.path)
            if mode == "timeout":
                time.sleep(TIMEOUT + 1.5)
            elif mode == "404":
                self.send(404)
            elif mode == "empty":
                self.send(200)
            elif mode == "reset":
                self.connection.close()
            else:
                return False
            return True

        def do_POST(self):
            if self.outage():
                return
            self.send(401, b'{"error":"token"}') if self.path == "/wishes/v1/wish" else self.send(404)

        def do_GET(self):
            p, rng = self.path, self.headers.get("Range")
            if self.outage():
                return
            if state.get("down") == p.split("/")[1]:
                return self.send(503)
            if p == "/site/":
                return self.send(200, b"<title>AurOS</title>")
            if p == "/site/release.json":
                return self.send(200, state["pin"]) if "pin" in state else self.send(404)
            if p == "/get/AurOS-Setup.exe" or p == "/get/piece":
                if state.get("range200") and rng:
                    return self.send(200, exe)
                if rng:
                    return self.send(206, exe[:1], [("Content-Range", f"bytes 0-0/{len(exe)}")])
                return self.send(200, exe)
            if p == "/get/SHA256SUMS":
                return self.send(200, f"{state.get('sum', good_sum)}  AurOS-Setup.exe\n".encode())
            if p == "/get/pieces.txt":
                base = f"http://127.0.0.1:{self.server.server_port}/get/"
                return self.send(200, f"base {base}\npiece piece {len(exe)} {good_sum}\n".encode())
            if p == "/apt/InRelease":
                return self.send(200, holder["inrelease"])
            if p == "/wishes/v1/health":
                return self.send(200, state.get("health", b'{"ok":true}'))
            if p == "/flathub/config":
                return self.send(200, b"[core]")
            if p == "/gate/_ops/state":
                return self.send(200, json.dumps(state.get("gate", {"paused": False, "wave": 1})).encode())
            if p == "/shut/AurOS-Setup.exe":
                return self.send(503, b"full", [("x-auros-gate", state.get("shut", "full"))])
            self.send(404)

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
    srv.handle_error = lambda *a: None  # a client that gave up is the test, not a traceback
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    u = f"http://127.0.0.1:{srv.server_port}"
    env = dict(os.environ, MON_SITE=u + "/site/", MON_GET=u + "/get/AurOS-Setup.exe", MON_APT=u + "/apt/", MON_WISHES=u + "/wishes",
               MON_FLATHUB=u + "/flathub/config", MON_FAST="1", AUROS_DOMAIN="", MON_GATE=u + "/gate")
    tmp = tempfile.mkdtemp()
    env["MON_LIVE_FILE"] = os.path.join(tmp, "live.txt")
    open(env["MON_LIVE_FILE"], "w").write("site download pieces channel wishes\n")

    keyhome = None
    if gpg:
        keyhome = os.path.join(tmp, "k"); os.mkdir(keyhome, 0o700)
        subprocess.run(["gpg", "--batch", "--homedir", keyhome, "--passphrase", "", "--quick-gen-key", "AurOS test", "ed25519", "sign", "1d"], check=True, capture_output=True)
        asc = subprocess.run(["gpg", "--batch", "--homedir", keyhome, "--armor", "--export"], check=True, capture_output=True).stdout
        env["MON_KEYRING"] = os.path.join(tmp, "kr.asc"); open(env["MON_KEYRING"], "wb").write(asc)
        rel = "Origin: AurOS\nSuite: stable\nDate: Thu, 22 Oct 2026 00:00:00 UTC\n"
        holder["inrelease"] = sign(tmp, rel, keyhome)

    last = []

    def states(deep=False):
        last[:] = run(deep, env)
        return {r["check"]: r["state"] for r in last}

    def expect(what, got, want):
        nonlocal ok
        good = all(got.get(k) == v for k, v in want.items())
        print(("ok   " if good else "FAIL ") + what + ("" if good else f"   got {got}, wanted {want}; " + "; ".join(r["detail"] for r in last if r["detail"] and want.get(r["check"]) not in (None, r["state"]))))
        ok = ok and good

    all_ok = dict(site="ok", download="ok", pieces="ok", wishes="ok", flathub="ok", **({"channel": "ok"} if gpg else {}))
    expect("everything up: all ok", states(deep=True), all_ok)
    state.update(down="site"); expect("site answers 503 -> site fails", states(), {"site": "fail", "download": "ok"}); state.clear()
    state.update(sum="0" * 64); expect("SHA256SUMS does not match the exe (deep) -> download fails", states(deep=True), {"download": "fail"})
    expect("... and the hourly deep check is what finds it (shallow passes)", states(deep=False), {"download": "ok"}); state.pop("sum")
    state.update(pin=json.dumps({"tag": "v0", "sha256": good_sum}).encode()); expect("release.json pins the exe's hash (deep) -> download ok", states(deep=True), {"download": "ok"})
    state.update(pin=json.dumps({"tag": "v0", "sha256": "1" * 64}).encode()); expect("exe and SHA256SUMS agree but release.json pins another hash (a release swapped with its sums) -> download fails", states(deep=True), {"download": "fail"})
    expect("... only the deep check reads it", states(deep=False), {"download": "ok"})
    state.update(pin=b"not json"); expect("release.json is garbage (deep) -> download fails", states(deep=True), {"download": "fail"}); state.pop("pin")
    state.update(range200=True); expect("a piece answers a Range request with 200, not 206 -> pieces fails", states(), {"pieces": "fail"}); state.pop("range200")
    state.update(health=b'{"ok":false}'); expect("wishes health says not ok -> wishes fails", states(), {"wishes": "fail"}); state.pop("health")
    state.update(down="wishes"); expect("wishes answers 503 -> wishes fails", states(), {"wishes": "fail"}); state.pop("down")
    if gpg:
        holder["inrelease"] = sign(tmp, rel + "Valid-Until: Fri, 23 Oct 2026 00:00:00 UTC\n", keyhome)
        expect("Release expires within 3 days (or already did) -> channel fails", states(), {"channel": "fail"})
        holder["inrelease"] = sign(tmp, rel + "Valid-Until: Tue, 01 Dec 2099 00:00:00 UTC\n", keyhome)
        expect("Release valid until 2099 -> channel ok", states(), {"channel": "ok"})
        holder["inrelease"] = holder["inrelease"].replace(b"Origin: AurOS", b"Origin: Evil")
        expect("InRelease altered after signing -> channel fails", states(), {"channel": "fail"})
        keyhome2 = os.path.join(tmp, "k2"); os.mkdir(keyhome2, 0o700)
        subprocess.run(["gpg", "--batch", "--homedir", keyhome2, "--passphrase", "", "--quick-gen-key", "Other", "ed25519", "sign", "1d"], check=True, capture_output=True)
        holder["inrelease"] = sign(tmp, rel, keyhome2)
        expect("signed by a key that is not the committed one -> channel fails", states(), {"channel": "fail"})
    else:
        print("SKIP the channel cases: no gpg here (they run in CI and on homebase)")
    # not launched yet: failing is only "pending", and nobody is paged
    open(env["MON_LIVE_FILE"], "w").write("wishes\n")
    state.update(down="site")
    expect("site down but not in live.txt -> pending, not fail", states(), {"site": "pending", "wishes": "ok"})
    expect("... and the run is still green", {"ok": str(summary(run(False, env))["ok"])}, {"ok": "True"})
    state.clear()
    env["MON_FLATHUB"] = u + "/nowhere"
    open(env["MON_LIVE_FILE"], "w").write("flathub wishes\n")
    expect("flathub down is information only -> warn, never fail", states(), {"flathub": "warn"})
    # The gate: shut on purpose is not an outage; a pause pages.
    open(env["MON_LIVE_FILE"], "w").write("site download pieces channel wishes gate\n")
    expect("the gate is open: gate ok", states(), {"gate": "ok"})
    env["MON_GET"] = u + "/shut/AurOS-Setup.exe"
    expect("the installer is full for today (503 with the gate's header): download is still ok", states(), {"download": "ok"})
    env["MON_GET"] = u + "/get/AurOS-Setup.exe"
    state["gate"] = {"paused": True, "reason": "stop rule: suspected data loss reported (1)", "wave": 1}
    expect("downloads are paused -> gate fails, and says the reason", states(), {"gate": "fail"})
    ok = ok and "data loss" in next(r["detail"] for r in last if r["check"] == "gate")
    state["modes"] = {"/gate/_ops/state": "404"}; state.pop("gate")
    expect("the gate's state is unreadable -> gate fails", states(), {"gate": "fail"})
    state.clear()
    env["MON_GET"] = u + "/shut/AurOS-Setup.exe"; env["MON_GATE"] = ""
    got = states()
    expect("no domain, no gate: the check is left out", {"n": str(sum(r["check"] == "gate" for r in last))}, {"n": "0"})
    env["MON_GET"] = u + "/get/AurOS-Setup.exe"; env["MON_GATE"] = u + "/gate"
    open(env["MON_LIVE_FILE"], "w").write("site download pieces channel wishes\n")
    # Every endpoint, broken four ways: the run must finish, say DOWN with a reason, and never "the check itself broke".
    globals()["TIMEOUT"] = 0.4
    open(env["MON_LIVE_FILE"], "w").write("site download pieces channel wishes\n")
    cases = [("/site/", "site", False), ("/get/AurOS-Setup.exe", "download", False), ("/get/SHA256SUMS", "download", True),
             ("/get/pieces.txt", "pieces", False), ("/get/piece", "pieces", False), ("/apt/InRelease", "channel", False),
             ("/wishes/v1/health", "wishes", False), ("/wishes/v1/wish", "wishes", False)]
    for path, check, deep in cases:
        for mode in ("timeout", "404", "empty", "reset"):
            if check == "channel" and not gpg:
                continue
            state["modes"] = {path: mode}
            got = states(deep)
            why = next(r["detail"] for r in last if r["check"] == check)
            expect(f"{path} {mode} -> {check} DOWN with a reason", got, {check: "fail"})
            good = bool(why) and "itself broke" not in why
            print(("ok   " if good else "FAIL ") + f"   ... and says why: {why[:90]}")
            ok = ok and good
    state.clear()
    dead = socket.socket(); dead.bind(("127.0.0.1", 0)); gone = f"http://127.0.0.1:{dead.getsockname()[1]}"; dead.close()
    env2 = dict(env, MON_SITE=gone + "/", MON_GET=gone + "/get/AurOS-Setup.exe", MON_APT=gone + "/apt/", MON_WISHES=gone)
    res = run(True, env2)
    expect("nothing listens at all: every live check DOWN, none crashed", {r["check"]: r["state"] for r in res},
           dict(site="fail", download="fail", pieces="fail", channel="fail", wishes="fail"))
    good = all(r["detail"] and "itself broke" not in r["detail"] for r in res if r["state"] == "fail")
    print(("ok   " if good else "FAIL ") + "   ... each with a reason"); ok = ok and good

    # What pages: a fake gh records what the monitor asks of GitHub.
    fakebin = os.path.join(tmp, "bin"); os.mkdir(fakebin)
    gl, gq = os.path.join(tmp, "gh.log"), os.path.join(tmp, "gh.list")
    open(os.path.join(fakebin, "gh"), "w").write('#!/bin/sh\necho "$@" >> "$GH_LOG"\n[ "$1 $2" = "issue list" ] && cat "$GH_LIST"\nexit 0\n')
    os.chmod(os.path.join(fakebin, "gh"), 0o755)
    genv = dict(os.environ, PATH=fakebin + os.pathsep + os.environ["PATH"], GH_LOG=gl, GH_LIST=gq, GITHUB_REPOSITORY="own/rep", GITHUB_REPOSITORY_OWNER="own",
                AUROS_SECOND_RESPONDER="helper")
    saved = dict(os.environ); os.environ.update(genv)
    bad = [dict(check="site", state="fail", detail="x answered 503", ms=1), dict(check="wishes", state="ok", detail="", ms=1),
           dict(check="channel", state="pending", detail="404", ms=1)]
    open(gq, "w").write("[]"); open(gl, "w").write("")
    issues(bad); log = open(gl).read()
    creates = [l for l in log.splitlines() if l.startswith("issue create")]
    expect("a live failure opens one issue, assigned to the owner and the second responder",
           {"n": len(creates), "owner": "--assignee own" in log, "second": "--assignee helper" in log, "label": "--label monitor" in log},
           {"n": 1, "owner": True, "second": True, "label": True})
    open(gl, "w").write(""); open(gq, "w").write('[{"number": 7, "title": "monitor: site is down"}]')
    issues(bad)
    expect("... the same failure again opens no second issue", {"creates": sum(l.startswith("issue create") for l in open(gl).read().splitlines())}, {"creates": 0})
    open(gl, "w").write("")
    issues([dict(check="site", state="ok", detail="", ms=1)])
    expect("... and recovery closes it", {"closed": "issue close 7" in open(gl).read()}, {"closed": True})
    os.environ.clear(); os.environ.update(saved)
    srv.shutdown()
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    results = run(deep="--deep" in argv)
    s = summary(results)
    for r in results:
        print(f"{r['state'].upper():8} {r['check']:9} {r['ms']:5} ms  {r['detail']}")
    if out:
        json.dump(s, open(out, "w"), indent=1)
    if "--issues" in argv:
        issues(results)
    pend = [r["check"] for r in results if r["state"] == "pending"]
    if pend:
        print(f"not launched yet (not paging): {', '.join(pend)}. They page once they are listed in docs/ops/live.txt.")
    return 0 if s["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
