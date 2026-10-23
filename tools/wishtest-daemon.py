#!/usr/bin/env python3
"""wishtest-daemon.py -- aurwish, the whole program, against fakes.

Builds the test build of aurwish (src/aurwish/build.sh, TEST=1: ASan + UBSan,
paths from the environment) and drives it exactly as the shell and the world
will: over its unix socket, with a fake Wishes service on 127.0.0.1 (which
checks the proof of work, the bearer token and what is sent), a fake
iio-sensor-proxy on a private D-Bus, fake sysfs for the battery and the
backlight, and fake wpctl / mpv / aurvoice that record what they were asked.

Needs Linux with dbus-daemon, libdbus-1-dev and curl.

    python3 tools/wishtest-daemon.py [scenario ...]
"""
import hashlib, http.server, json, os, shutil, signal, socket, stat, subprocess, sys, tempfile, threading, time
from datetime import datetime, timedelta, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("  FAIL", msg)
    return cond


# ── the fake service ───────────────────────────────────────────────────

class Cloud:
    def __init__(self):
        self.log = []                 # (path, headers, body-json or raw)
        self.tokens = set()
        self.bits = 10                # fast for tests; the daemon starts at 10 here (21 in the potato test)
        self.day_offset = 0           # how far the server's idea of today is from UTC today
        self.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "other"})
        self.ask = lambda req: (200, {}, {"help_page_id": None})
        self.delay = 0
        self.expire_next = False      # answer the next /wish with 401
        self.registers = 0
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def _send(self, code, headers, obj=None, raw=None):
                body = raw if raw is not None else (json.dumps(obj).encode() if obj is not None else b"")
                self.send_response(code)
                for k, v in headers.items():
                    self.send_header(k, v)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(body)

            def do_POST(self):
                n = int(self.headers.get("Content-Length", 0))
                raw = self.rfile.read(n)
                try:
                    req = json.loads(raw)
                except Exception:
                    req = None
                outer.log.append((self.path, dict(self.headers), req if req is not None else raw))
                if outer.delay:
                    time.sleep(outer.delay)
                if self.path == "/v1/register":
                    return self._register(req)
                auth = self.headers.get("Authorization", "")
                if not auth.startswith("Bearer ") or auth[7:] not in outer.tokens:
                    return self._send(401, {}, {"error": "token"})
                if self.path == "/v1/forget":
                    outer.tokens.discard(auth[7:])
                    return self._send(204, {})
                if outer.expire_next and self.path == "/v1/wish":
                    outer.expire_next = False
                    outer.tokens.discard(auth[7:])
                    return self._send(401, {}, {"error": "token"})
                fn = outer.wish if self.path == "/v1/wish" else outer.ask
                code, hdr, obj = fn(req)
                if isinstance(obj, bytes):
                    return self._send(code, hdr, raw=obj)
                return self._send(code, hdr, obj)

            def _register(self, req):
                outer.registers += 1
                day = (datetime.now(timezone.utc) + timedelta(days=outer.day_offset)).strftime("%Y-%m-%d")
                ok = isinstance(req, dict) and isinstance(req.get("pow"), dict)
                if ok:
                    p = req["pow"]
                    msg = f"auros-register-v1:{p.get('day')}:{req.get('install_id')}:{p.get('nonce')}".encode()
                    h = int.from_bytes(hashlib.sha256(msg).digest(), "big")
                    ok = (p.get("day") == day and 0 < len(str(p.get("nonce"))) <= 32 and
                          h >> (256 - outer.bits) == 0)
                if not ok:
                    return self._send(400, {}, {"error": "pow", "day": day, "bits": outer.bits})
                tok = hashlib.sha256(os.urandom(8)).hexdigest()[:22] + "." + hashlib.sha256(os.urandom(8)).hexdigest()[:43]
                outer.tokens.add(tok)
                return self._send(200, {}, {"token": tok, "expires_at": int(time.time()) + 90 * 86400})

        class Srv(http.server.ThreadingHTTPServer):
            def handle_error(self, request, client_address):
                pass                      # curl hanging up on a refused reply is not news

        self.srv = Srv(("127.0.0.1", 0), H)
        self.port = self.srv.server_address[1]
        self.thread = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.thread.start()

    def url(self):
        return f"http://127.0.0.1:{self.port}"

    def stop(self):
        self.srv.shutdown()
        self.srv.server_close()

    def paths(self):
        return [p for p, _, _ in self.log]


# ── the world ─────────────────────────────────────────────────────────────

ENVS = []


class Env:
    def __init__(self, bins, wishes="on", with_dbus=False, sensors="compass,tilt,light", cloud=None, features="on"):
        ENVS.append(self)
        self.bins = bins
        self.d = tempfile.mkdtemp(prefix="wishd-")
        d = self.d
        for sub in ("home/.local/share/applications", "run", "bin", "sys/backlight/intel", "sys/power/BAT0",
                    "sys/power/AC", "themes", "help", "sounds", "log", "etc"):
            os.makedirs(os.path.join(d, sub), exist_ok=True)
        self.home = os.path.join(d, "home")
        self.run = os.path.join(d, "run")
        os.chmod(self.run, 0o700)
        self.sock = os.path.join(self.run, "aurwish.sock")
        self.store = os.path.join(d, "home/.local/share/auros/wishes.json")
        os.makedirs(os.path.dirname(self.store), exist_ok=True)
        self.token_file = os.path.join(os.path.dirname(self.store), "wishes-token.json")
        self.policy = os.path.join(d, "etc/policy.conf")
        open(self.policy, "w").write(f"profile = test\n" + (f"wishes = {wishes}\n" if wishes is not None else ""))
        self.features = os.path.join(d, "etc/features")
        if features is not None:
            open(self.features, "w").write(f"wishes={features}\n")
        self.conf = os.path.join(d, "etc/wishes.conf")
        self.cloud = cloud
        open(self.conf, "w").write(f"url={cloud.url()}\n" if cloud else "")
        open(os.path.join(d, "home/.local/share/applications/firefox.desktop"), "w").write(
            "[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox\n")
        open(os.path.join(d, "themes/sandstone.theme"), "w").write('theme_name="Sandstone"\n')
        open(os.path.join(d, "help/printing.html"), "w").write("<p>Printing</p>")
        open(os.path.join(d, "sounds/chime.wav"), "wb").write(b"RIFF")
        # a battery and a backlight
        self.set_battery(80, "Discharging", 0)
        b = os.path.join(d, "sys/backlight/intel")
        open(os.path.join(b, "type"), "w").write("raw\n")
        open(os.path.join(b, "max_brightness"), "w").write("100\n")
        open(os.path.join(b, "brightness"), "w").write("50\n")
        self.backlight = os.path.join(b, "brightness")
        # the sound server's socket (so power.c looks for it) and fake programs
        open(os.path.join(self.run, "pipewire-0"), "w").close()
        self.fake("wpctl", 'case "$1" in get-volume) echo "Volume: 0.40";; *) echo "$@" >> "$LOGDIR/wpctl.log";; esac')
        self.fake("mpv", 'echo "$@" >> "$LOGDIR/mpv.log"')
        self.fake("aurvoice", 'echo "$@" >> "$LOGDIR/aurvoice.log"')
        self.proc = None
        self.bus = None
        self.sp = None
        self.ctl = os.path.join(d, "sensors.ctl")
        self.splog = os.path.join(d, "log/sensor-claims.log")
        if with_dbus:
            self.start_dbus(sensors)

    def fake(self, name, body):
        p = os.path.join(self.d, "bin", name)
        open(p, "w").write("#!/bin/sh\n" + body + "\n")
        os.chmod(p, 0o755)

    def logtext(self, name):
        try:
            return open(os.path.join(self.d, "log", name)).read()
        except FileNotFoundError:
            return ""

    def set_battery(self, pct, status, mains):
        b = os.path.join(self.d, "sys/power/BAT0")
        open(os.path.join(b, "type"), "w").write("Battery\n")
        open(os.path.join(b, "capacity"), "w").write(f"{pct}\n")
        open(os.path.join(b, "status"), "w").write(status + "\n")
        open(os.path.join(b, "present"), "w").write("1\n")
        a = os.path.join(self.d, "sys/power/AC")
        open(os.path.join(a, "type"), "w").write("Mains\n")
        open(os.path.join(a, "online"), "w").write(f"{mains}\n")

    def start_dbus(self, sensors):
        bus_path = os.path.join(self.d, "bus")
        conf = os.path.join(self.d, "bus.conf")
        open(conf, "w").write(
            "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
            "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\"><busconfig><type>session</type>"
            f"<listen>unix:path={bus_path}</listen><auth>EXTERNAL</auth>"
            "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
            "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>")
        self.bus = subprocess.Popen(["dbus-daemon", "--config-file=" + conf, "--nofork"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(bus_path):
                break
            time.sleep(0.05)
        self.addr = "unix:path=" + bus_path
        open(self.ctl, "w").write(f"has={sensors}\nheading=180\norient=normal\nlux=500\n")
        self.sp = subprocess.Popen([self.bins["sensorproxy"], self.addr, self.ctl, self.splog],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.sp.stdout.readline()                         # "ready"

    def sensor(self, **kw):
        cur = dict(l.strip().split("=", 1) for l in open(self.ctl) if "=" in l)
        cur.update({k: str(v) for k, v in kw.items()})
        open(self.ctl, "w").write("".join(f"{k}={v}\n" for k, v in cur.items()))

    def env(self, extra=None):
        e = dict(os.environ)
        e.update({
            "HOME": self.home, "XDG_RUNTIME_DIR": self.run, "XDG_DATA_HOME": os.path.join(self.home, ".local/share"),
            "PATH": os.path.join(self.d, "bin") + ":" + e.get("PATH", "/usr/bin:/bin"),
            "LOGDIR": os.path.join(self.d, "log"),
            "AURWISH_SOCK": self.sock, "AURWISH_STORE": self.store, "AURWISH_CONF": self.conf,
            "AURWISH_POLICY": self.policy, "AURWISH_FEATURES": self.features, "AURWISH_HELP": os.path.join(self.d, "help"),
            "AURWISH_SOUNDS": os.path.join(self.d, "sounds"),
            "AUROS_BACKLIGHT": os.path.join(self.d, "sys/backlight"), "AUROS_POWER_SUPPLY": os.path.join(self.d, "sys/power"),
            "AUROS_THEME_DIR": os.path.join(self.d, "themes"),
            "AURWISH_CURL_ERR": os.path.join(self.d, "log/curl.err"),
            "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=0", "UBSAN_OPTIONS": "halt_on_error=1",
        })
        if self.bus:
            e["AURWISH_DBUS"] = self.addr
        e.setdefault("AURWISH_SLOW_TICK_MS", "1000")
        e.setdefault("AURWISH_POW_BITS", os.environ.get("AURWISH_POW_BITS", "10"))
        e.update(extra or {})
        return e

    def start(self, force=True, extra=None):
        args = [self.bins["aurwish"]] + (["--force"] if force else [])
        self.proc = subprocess.Popen(args, env=self.env(extra), stderr=subprocess.PIPE, text=True)
        for _ in range(200):
            if os.path.exists(self.sock) or self.proc.poll() is not None:
                break
            time.sleep(0.05)
        return self.proc

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        err = self.proc.stderr.read() if self.proc and self.proc.stderr else ""
        self.proc = None
        return err

    def close(self):
        err = self.stop()
        self.last_err = (getattr(self, "last_err", "") + err)
        self.curl_err = self.logtext("curl.err") or getattr(self, "curl_err", "")
        for p in (self.sp, self.bus):
            if p:
                p.kill()
                p.wait()
        shutil.rmtree(self.d, ignore_errors=True)
        return err


class Client:
    def __init__(self, env, hello=True):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(1.0)
        self.s.connect(env.sock)
        self.buf = b""
        self.events = []
        self.n = 0
        if hello:
            self.hello = self.call({"op": "hello"})

    def send(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())

    def lines(self, timeout):
        end = time.time() + timeout
        out = []
        while time.time() < end:
            try:
                k = self.s.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                break
            if not k:
                break
            self.buf += k
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                out.append(json.loads(l))
        return out

    def call(self, obj, timeout=30):
        self.n += 1
        obj = dict(obj, id=self.n)
        self.send(obj)
        end = time.time() + timeout
        while time.time() < end:
            self.events += self.lines(0.2)
            for m in self.events:
                if m.get("id") == self.n:
                    self.events.remove(m)
                    return m
        return None

    def wait_event(self, ev, timeout=6):
        end = time.time() + timeout
        while time.time() < end:
            for m in self.events:
                if m.get("ev") == ev:
                    self.events.remove(m)
                    return m
            self.events += self.lines(0.2)
        return None

    def no_event(self, ev, timeout=2):
        end = time.time() + timeout
        while time.time() < end:
            self.events += self.lines(0.2)
            if any(m.get("ev") == ev for m in self.events):
                return False
        return True

    def close(self):
        self.s.close()


POTATO = {"v": 1, "when": {"type": "typed_word", "word": "potato"},
          "do": [{"type": "fill_screen", "emoji": "\U0001F954", "seconds": 3}], "enabled": True}
COMPASS = {"v": 1, "when": {"type": "sensor", "sensor": "compass", "heading": "north", "tolerance_deg": 20},
           "do": [{"type": "brightness", "set_percent": 100}], "enabled": True}


def wish_says(c, text, doc=None, cloud=None, **kw):
    if cloud and doc is not None:
        cloud.wish = lambda req: (200, {}, {"kind": "wish", "wish": doc})
    return c.call(dict({"op": "wish", "text": text, "lang": "en", "voice": False}, **kw))


def accept(c, pending):
    return c.call({"op": "confirm", "pending": pending})


# ── the scenarios ─────────────────────────────────────────────────────────

def s_policy_off(bins):
    """D14: nothing runs unless the machine says wishes=on (and policy does not forbid it)."""
    def starts(**kw):
        e = Env(bins, **kw)
        p = e.start(force=False)
        try:
            p.wait(3)
            ran = False
        except subprocess.TimeoutExpired:
            ran = True
        rc = p.returncode
        sock = os.path.exists(e.sock)
        if ran:
            p.send_signal(signal.SIGTERM)
            p.wait(5)
        e.close()
        return ran and sock, rc

    check(starts(features="off")[0] is False, "features wishes=off: does not start")
    ok, rc = starts(features="off")
    check(rc == 0, "... and exits 0, so systemd does not restart it")
    check(starts(features=None)[0] is False, "no features file: does not start")
    check(starts(features="ON")[0] is False, "wishes=ON (wrong case) is not on")
    check(starts(features="yes")[0] is False, "wishes=yes is not on")
    check(starts(features="on", wishes="off")[0] is False, "policy wishes = off beats features")
    check(starts(features="on", wishes="no")[0] is False, "policy wishes = no is not on")
    check(starts(features="on", wishes="")[0] is False, "policy wishes = (empty) is not on")
    check(starts(features="on", wishes="on")[0] is True, "features on + policy on: starts")
    check(starts(features="on", wishes=None)[0] is True, "features on, policy silent: starts")
    e = Env(bins, features="on", wishes="on")
    os.remove(e.policy)
    p = e.start(force=False)
    check(p.poll() is None and os.path.exists(e.sock), "features on, no policy file: starts")
    check(stat.S_IMODE(os.stat(e.sock).st_mode) == 0o600, "the socket is mode 0600")
    p.send_signal(signal.SIGTERM)
    p.wait(5)
    check(p.returncode == 0 and not os.path.exists(e.sock), "SIGTERM: clean exit, socket removed")
    e.close()


def s_potato(bins):
    """The owner's first wish, end to end -- with the real proof of work (21 bits)."""
    cloud = Cloud()
    cloud.bits = 21
    e = Env(bins, cloud=cloud)
    e.start(extra={"AURWISH_POW_BITS": "21"})
    c = Client(e)
    check(c.hello["kind"] == "hello" and c.hello["consent"] is False, "hello: no consent yet")
    ev = c.wait_event("triggers")
    check(ev == {"ev": "triggers", "words": [], "apps": []}, f"empty trigger list at first: {ev}")
    r = wish_says(c, "fill my screen with potatoes when I type potato", POTATO, cloud)
    check(r["kind"] == "error" and r["error"] == "consent", "no wish is sent before she agrees")
    check(cloud.log == [], "nothing at all reached the service without consent")
    check(c.call({"op": "consent", "version": 1, "yes": True})["kind"] == "done", "consent")
    t0 = time.time()
    r = wish_says(c, "fill my screen with potatoes when I type potato", POTATO, cloud)
    print(f"  (register with 21 bits of work + wish: {time.time() - t0:.1f} s, sanitizers on)")
    check(r and r["kind"] == "wish" and "Every time you type the word *potato*" in r["sentence"], f"the device's sentence: {r}")
    check(cloud.paths() == ["/v1/register", "/v1/wish"], f"register then wish: {cloud.paths()}")
    req = cloud.log[1][2]
    check(set(req) == {"text", "lang", "schema", "sensors"}, f"the request carries only what §5 allows: {set(req)}")
    check(req["text"] == "fill my screen with potatoes when I type potato" and req["lang"] == "en", "her words and language")
    check("typed_word" in req["schema"]["triggers"] and "fill_screen" in req["schema"]["actions"] and
          "install_program" not in req["schema"]["actions"], "the schema field")
    check(cloud.log[1][1].get("Authorization", "").startswith("Bearer "), "the token is sent as a bearer")
    check(cloud.log[0][1].get("Authorization") is None, "registering sends no token")
    check(cloud.registers == 1, "registered once")
    check(stat.S_IMODE(os.stat(e.token_file).st_mode) == 0o600, "the token file is 0600")
    done = accept(c, r["pending"])
    check(done["kind"] == "done" and done["wid"] == "w1", "Yes")
    ev = c.wait_event("triggers")
    check(ev and ev["words"] == ["potato"], f"the compositor is told the word: {ev}")
    c.send({"op": "typed", "word": "potatoes"})
    check(c.no_event("fill", 1.5), "potatoes: nothing")
    c.send({"op": "typed", "word": "potato"})
    f = c.wait_event("fill")
    check(f and f["emoji"] == "\U0001F954" and f["seconds"] == 3 and f["wid"] == "w1", f"potatoes fill the screen: {f}")
    check(f and "Stop this wish" not in json.dumps(f), "the card text is the shell's to write")
    check(stat.S_IMODE(os.stat(e.store).st_mode) == 0o600, "wishes.json is 0600")
    # a second wish does not register again
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "money"})
    r = c.call({"op": "wish", "text": "send my sister fifty pounds", "lang": "en"})
    check(r["kind"] == "cannot" and r["missing"] == "money" and "cannot spend or move money" in r["text"], f"cannot: {r}")
    check(cloud.registers == 1, "the token is reused")
    # the file survives a restart, and the word is still registered
    c.close()
    e.stop()
    e.start()
    c = Client(e)
    ev = c.wait_event("triggers")
    check(ev and ev["words"] == ["potato"], "after a restart the wish is still there")
    c.send({"op": "typed", "word": "potato"})
    check(c.wait_event("fill") is not None, "and still works")
    # Stop this wish
    r = c.call({"op": "list"})
    check(len(r["wishes"]) == 1 and r["wishes"][0]["status"] == "on", "My wishes")
    c.call({"op": "enable", "wid": "w1", "on": False})
    ev = c.wait_event("triggers")
    check(ev and ev["words"] == [], "stopped: the word is withdrawn")
    c.close()
    e.close()
    cloud.stop()


def s_compass(bins):
    """The owner's second wish: the compass, over a real D-Bus."""
    cloud = Cloud()
    e = Env(bins, with_dbus=True, cloud=cloud)
    e.start()
    c = Client(e)
    check(c.hello["sensors"] == {"compass": True, "tilt": True, "light": True}, f"sensors found: {c.hello['sensors']}")
    c.call({"op": "consent", "version": 1, "yes": True})
    r = wish_says(c, "when my laptop points north make the screen brighter", COMPASS, cloud)
    check(r["kind"] == "wish" and "points *north*" in r["sentence"] and "*100%*" in r["sentence"], f"{r}")
    check(cloud.log[-1][2]["sensors"] == {"compass": True, "tilt": True, "light": True}, "the cloud is told what exists")
    check("ClaimCompass" not in e.logtext("sensor-claims.log"), "no claim before she says yes")
    accept(c, r["pending"])
    time.sleep(2.5)
    check("ClaimCompass" in e.logtext("sensor-claims.log"), "claimed once a wish needs it")
    e.sensor(heading=170)
    time.sleep(1.5)
    e.sensor(heading=15)                          # inside 20 degrees of north
    for _ in range(40):
        if open(e.backlight).read().strip() == "100":
            break
        time.sleep(0.2)
    check(open(e.backlight).read().strip() == "100", f"the screen got brighter: {open(e.backlight).read().strip()}")
    c.call({"op": "remove", "wid": "w1"})
    time.sleep(2.5)
    check("ReleaseCompass" in e.logtext("sensor-claims.log"), "released when the last such wish goes")
    c.close()
    e.close()
    cloud.stop()


def s_compass_missing(bins):
    cloud = Cloud()
    e = Env(bins, with_dbus=True, sensors="light", cloud=cloud)
    e.start()
    c = Client(e)
    check(c.hello["sensors"] == {"compass": False, "tilt": False, "light": True}, "only the light sensor")
    c.call({"op": "consent", "version": 1, "yes": True})
    r = wish_says(c, "when my laptop points north make the screen brighter", COMPASS, cloud)
    check(r["kind"] == "cannot" and r["missing"] == "compass" and "no compass" in r["text"], f"cannot, missing compass: {r}")
    c.close()
    e.close()
    cloud.stop()
    e = Env(bins)                                   # no sensor service at all
    e.start()
    c = Client(e)
    check(c.hello["sensors"] == {"compass": False, "tilt": False, "light": False}, "no iio-sensor-proxy: no sensors")
    c.close()
    e.close()


def s_errors(bins):
    cloud = Cloud()
    e = Env(bins, cloud=cloud)
    e.start()
    c = Client(e)
    c.call({"op": "consent", "version": 1, "yes": True})

    def ask(text="make it brighter please", **kw):
        return c.call(dict({"op": "wish", "text": text, "lang": "en"}, **kw), timeout=25)

    cloud.wish = lambda req: (429, {"Retry-After": "120"}, {"retry_after": 120, "text": "<script>evil</script>"})
    r = ask("one")
    check(r["kind"] == "error" and r["error"] == "limit" and r["retry_after"] == 120 and
          "lots of wishes today" in r["text"] and "script" not in r["text"], f"429: {r}")
    cloud.wish = lambda req: (503, {"Retry-After": "40"}, {"retry_after": 40})
    r = ask("two thing")
    check(r["error"] == "resting" and r["retry_after"] == 40, f"503: {r}")
    cloud.wish = lambda req: (400, {}, {"error": "request"})
    check(ask("three thing")["error"] == "refused", "400")
    cloud.wish = lambda req: (500, {}, b"<html>boom</html>")
    check(ask("four thing")["error"] == "broken", "500")
    cloud.wish = lambda req: (200, {}, b"not json at all")
    check(ask("five thing")["error"] == "broken", "garbage")
    cloud.wish = lambda req: (200, {}, b"{" * 5000)
    check(ask("six thing")["error"] == "broken", "deeply nested garbage")
    cloud.wish = lambda req: (200, {}, b"x" * 200000)
    check(ask("seven thing")["error"] in ("broken", "offline"), "a reply bigger than we read")
    cloud.wish = lambda req: (200, {}, {"kind": "banana"})
    check(ask("eight thing")["error"] == "broken", "unknown kind")
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "Your PC is infected, call 0800 123 456"})
    r = ask("nine thing")
    check(r["kind"] == "cannot" and r["missing"] == "other" and "infected" not in json.dumps(r), f"cloud words never repeated: {r}")
    # sending rules
    cloud.log.clear()
    check(ask("hi")["error"] == "refused", "under 3 characters is not sent")
    check(ask("x" * 301)["error"] == "refused", "over 300 is not sent")
    check(cloud.log == [], "none of those reached the service")
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "other"})
    ask("same words again")
    n = len(cloud.log)
    check(ask("same words again")["error"] == "refused" and len(cloud.log) == n, "the same text within 30 s is not sent twice")
    # privacy: digits and e-mail addresses are replaced before sending
    ask("remind me to ring 0800 123 4567 or mail bob.smith@example.com")
    sent = cloud.log[-1][2]["text"]
    check("0800" not in sent and "@" not in sent and "[number]" in sent and "[email]" in sent, f"cleaned: {sent}")
    # 401 on a wish: register again, once, silently
    reg = cloud.registers
    cloud.expire_next = True
    r = ask("after the token expired")
    check(r["kind"] == "cannot" and cloud.registers == reg + 1, f"silent re-registration: {r} {cloud.registers}")
    # her clock is wrong: the service says its day and bits
    cloud.day_offset = 1
    os.remove(e.token_file) if os.path.exists(e.token_file) else None
    r = ask("with a wrong clock today")
    check(r["kind"] == "cannot", f"wrong clock is corrected from the 400: {r}")
    cloud.day_offset = 0
    # cancel while waiting
    cloud.delay = 3
    c.send({"op": "wish", "id": 900, "text": "this takes a while", "lang": "en"})
    time.sleep(0.5)
    c.send({"op": "cancel", "id": 900})
    time.sleep(0.5)
    cloud.delay = 0
    r = ask("right after cancelling")
    check(r["kind"] != "error" or r["error"] != "busy", f"cancel frees the line: {r}")
    # busy: one at a time
    cloud.delay = 2
    c.send({"op": "wish", "id": 901, "text": "first of two", "lang": "en"})
    time.sleep(0.4)
    c.send({"op": "wish", "id": 902, "text": "second of two", "lang": "en"})
    got = {}
    end = time.time() + 12
    while time.time() < end and len(got) < 2:
        for m in c.lines(0.3):
            if m.get("id") in (901, 902):
                got[m["id"]] = m
    check(got.get(902, {}).get("error") == "busy" and got.get(901, {}).get("kind") == "cannot", f"one at a time: {got}")
    cloud.delay = 0
    # the service goes away
    cloud.stop()
    r = ask("nobody is there to hear")
    check(r["kind"] == "error" and r["error"] == "offline" and "internet" in r["text"], f"offline: {r}")
    c.close()
    e.close()


def s_hostile_cloud(bins):
    """§7.1: the answers are treated as if an attacker wrote them."""
    cloud = Cloud()
    e = Env(bins, cloud=cloud)
    e.start()
    c = Client(e)
    c.call({"op": "consent", "version": 1, "yes": True})
    n = [0]

    def ask(doc):
        n[0] += 1
        cloud.wish = lambda req: (200, {}, {"kind": "wish", "wish": doc})
        return c.call({"op": "wish", "text": f"please show a kind note number {n[0]}", "lang": "en"})

    now = {"type": "now"}
    bad = [
        dict(POTATO, title="Free money"),
        {"v": 1, "when": now, "do": [{"type": "show_message", "text": "Your computer is infected. Call 0800 123 456"}], "enabled": True},
        {"v": 1, "when": now, "do": [{"type": "install_program", "flathub_id": "com.anydesk.Anydesk"}], "enabled": True},
        {"v": 1, "when": now, "do": [{"type": "run_command", "cmd": "rm -rf ~"}], "enabled": True},
        {"v": 1, "when": now, "do": [{"type": "open_website", "site": "evil.example"}], "enabled": True},
        {"v": 1, "when": {"type": "every", "minutes": 5}, "do": [{"type": "volume", "set_percent": 80}], "enabled": True},
        {"v": 1, "when": now, "do": [{"type": "brightness", "set_percent": 0}], "enabled": True},
        {"v": 1, "when": now, "do": [{"type": "fill_screen", "emoji": "\U0001F52B", "seconds": 3}], "enabled": True},
        {"v": 2, "when": now, "do": [{"type": "speak_time"}], "enabled": True},
    ]
    for doc in bad:
        r = ask(doc)
        check(r["kind"] == "cannot", f"refused: {json.dumps(doc)[:90]} -> {r.get('kind')}")
        check("infected" not in json.dumps(r) and "Free money" not in json.dumps(r), "the cloud's words are not repeated")
    # help pages: only a plain name of a page that exists
    for page, want in (("printing", "printing"), ("nothere", None), ("../../etc/passwd", None),
                       ("../help/printing", None), ("./printing", None),
                       ("Printing", None), ("printing/../printing", None), ("", None), (7, None)):
        cloud.ask = lambda req, p=page: (200, {}, {"help_page_id": p})
        r = c.call({"op": "ask", "text": f"how do I print number {len(str(page))}{page}", "lang": "en"})
        check(r.get("kind") == "help" and r.get("page") == want, f"help page {page!r}: {r}")
    # an alt_wish is offered only if it passes every rule here
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "compass", "alt_wish": dict(COMPASS, do=[{"type": "brightness", "set_percent": 100}])})
    r = c.call({"op": "wish", "text": "when my laptop points north do it", "lang": "en"})
    check(r["kind"] == "cannot" and "alt" not in r, f"alt_wish that needs a compass is not offered: {r}")
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "other",
                                         "alt_wish": {"v": 1, "when": now, "do": [{"type": "brightness", "set_percent": 80}], "enabled": True}})
    r = c.call({"op": "wish", "text": "make the screen look nicer", "lang": "en"})
    check(r["kind"] == "cannot" and r.get("alt", {}).get("pending") and "*80%*" in r["alt"]["sentence"], f"a valid alt_wish: {r}")
    c.close()
    e.close()
    cloud.stop()


def s_forget(bins):
    cloud = Cloud()
    e = Env(bins, cloud=cloud)
    e.start()
    c = Client(e)
    c.call({"op": "consent", "version": 1, "yes": True})
    r = wish_says(c, "fill my screen with potatoes when I type potato", POTATO, cloud)
    accept(c, r["pending"])
    first_id = json.load(open(e.token_file))["install_id"]
    check(c.call({"op": "forget"})["kind"] == "done", "forget")
    time.sleep(1)
    check("/v1/forget" in cloud.paths(), "the service is told to forget her")
    check(not os.path.exists(e.token_file), "the token is gone")
    check(len(json.load(open(e.store))["wishes"]) == 0, "wishes.json is emptied")
    check(cloud.log[-1][1].get("Authorization", "").startswith("Bearer "), "forget carries her token")
    c.call({"op": "consent", "version": 1, "yes": True})
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "other"})
    c.call({"op": "wish", "text": "something after forgetting", "lang": "en"})
    check(json.load(open(e.token_file))["install_id"] != first_id, "a new install id after forgetting")
    # turning Wishes off forgets too
    n = cloud.paths().count("/v1/forget")
    c.call({"op": "consent", "version": 1, "yes": False})
    time.sleep(1)
    check(cloud.paths().count("/v1/forget") == n + 1 and not os.path.exists(e.token_file), "turning Wishes off forgets her")
    c.close()
    e.close()
    cloud.stop()


def s_token_not_in_ps(bins):
    """The token must never be on a command line (ps shows everyone's)."""
    cloud = Cloud()
    e = Env(bins, cloud=cloud)
    e.start()
    c = Client(e)
    c.call({"op": "consent", "version": 1, "yes": True})
    cloud.wish = lambda req: (200, {}, {"kind": "cannot", "missing": "other"})
    c.call({"op": "wish", "text": "first one to get a token", "lang": "en"})
    tok = json.load(open(e.token_file))["token"]
    cloud.delay = 3
    c.send({"op": "wish", "id": 77, "text": "this one is slow", "lang": "en"})
    seen = False
    end = time.time() + 2.5
    saw_curl = False
    while time.time() < end:
        out = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True).stdout
        saw_curl |= "curl" in out and "AurOS-Wishes" in out
        seen |= tok in out or "Bearer" in out
        time.sleep(0.05)
    check(saw_curl, "the test did catch curl running")
    check(not seen, "the token and the word Bearer never appear in a process list")
    cloud.delay = 0
    c.close()
    e.close()
    cloud.stop()


def s_socket(bins):
    e = Env(bins)
    e.start()
    c = Client(e)
    check(c.call({"op": "format_disk"})["error"] == "refused", "unknown op")
    c.s.sendall(b"x" * 5000)                       # a line that never ends
    time.sleep(0.5)
    try:
        dead = c.s.recv(10) == b""
    except OSError:
        dead = True
    check(dead, "a line of more than 4096 bytes closes the connection")
    c2 = Client(e)
    c2.s.sendall(b"this is not json\n")
    time.sleep(0.5)
    try:
        dead = c2.s.recv(10) == b""
    except OSError:
        dead = True
    check(dead, "not JSON closes the connection")
    c3 = Client(e)
    c3.s.sendall(b'[1,2,3]\n')
    time.sleep(0.3)
    try:
        dead = c3.s.recv(10) == b""
    except OSError:
        dead = True
    check(dead, "JSON that is not an object closes the connection")
    cs = [Client(e) for _ in range(4)]
    extra = socket.socket(socket.AF_UNIX)
    extra.settimeout(1)
    extra.connect(e.sock)
    try:
        extra.sendall(b'{"op":"hello","id":1}\n')
        got = extra.recv(100)
    except OSError:
        got = b""
    check(got == b"", "a fifth connection is not served")
    for x in cs:
        x.close()
    # a shell that connects and never reads must not stop the engine
    slow = socket.socket(socket.AF_UNIX)
    slow.connect(e.sock)
    time.sleep(0.2)
    c4 = Client(e)
    check(c4.call({"op": "list"})["kind"] == "list", "still answering")
    c4.close()
    slow.close()
    e.close()


def s_sources(bins):
    """USB, the battery, startup and the camera."""
    cloud = Cloud()
    e = Env(bins, cloud=cloud)
    e.start()
    c = Client(e)
    c.call({"op": "consent", "version": 1, "yes": True})
    usb = {"v": 1, "when": {"type": "usb_inserted"}, "do": [{"type": "play_sound", "sound": "chime"}], "enabled": True}
    low = {"v": 1, "when": {"type": "battery_below", "percent": 20}, "do": [{"type": "brightness", "set_percent": 60}], "enabled": True}
    plug = {"v": 1, "when": {"type": "plugged_in"}, "do": [{"type": "night_light", "on": False}], "enabled": True}
    start = {"v": 1, "when": {"type": "startup"}, "do": [{"type": "fill_screen", "emoji": "\U0001F338", "seconds": 2}], "enabled": True}
    for i, doc in enumerate((usb, low, plug, start)):
        r = wish_says(c, f"a wish about number {i}", doc, cloud)
        check(r["kind"] == "wish", f"offer {i}: {r}")
        accept(c, r["pending"])
    c.send({"op": "inject", "what": "usb"})
    time.sleep(1.5)
    check("chime.wav" in e.logtext("mpv.log"), f"a USB stick plays the chime: {e.logtext('mpv.log')!r}")
    time.sleep(3.2)
    e.set_battery(15, "Discharging", 0)
    for _ in range(60):
        if open(e.backlight).read().strip() == "60":
            break
        time.sleep(0.25)
    check(open(e.backlight).read().strip() == "60", "the battery wish fires when it drops below 20%")
    e.set_battery(15, "Charging", 1)
    ev = None
    for _ in range(40):
        ev = [m for m in c.events if m.get("ev") == "shell" and m.get("do", {}).get("type") == "night_light"]
        c.events += c.lines(0.25)
        if ev:
            break
    c.events += c.lines(0.5)
    ev = [m for m in c.events if m.get("ev") == "shell" and m["do"]["type"] == "night_light"]
    check(ev and ev[0]["do"] == {"type": "night_light", "on": False}, f"plugging in is a shell action: {ev}")
    c.close()
    e.stop()
    # startup fires when the shell first says hello after a (re)start
    e.start()
    c = Client(e)
    f = c.wait_event("fill")
    check(f and f["emoji"] == "\U0001F338", f"startup wish at hello: {f}")
    c.close()
    e.stop()
    # the camera is on: nothing is shown
    e.start(extra={"AURWISH_FAKE_HOLD": "4"})
    c = Client(e)
    c.wait_event("triggers")
    pot = {"v": 1, "when": {"type": "typed_word", "word": "potato"}, "do": [{"type": "fill_screen", "emoji": "\U0001F954", "seconds": 3}], "enabled": True}
    r = wish_says(c, "show potatoes when I type potato", pot, cloud)
    accept(c, r["pending"])
    c.events = []
    c.send({"op": "typed", "word": "potato"})
    check(c.no_event("fill", 5), "the camera is on: no card")
    c.close()
    e.close()
    cloud.stop()


def s_corrupt_file(bins):
    e = Env(bins)
    open(e.store, "w").write("{\"v\":1,\"wishes\":[{\"id\":\"w1\",\"wish\":")
    e.start()
    c = Client(e)
    check(c.hello["kind"] == "hello", "a corrupt wishes.json never stops the engine")
    check(len(c.call({"op": "list"})["wishes"]) == 0, "it starts empty")
    kept = [f for f in os.listdir(os.path.dirname(e.store)) if "unreadable" in f]
    check(len(kept) == 1, "and the file is kept, not deleted")
    c.close()
    e.close()


SCENARIOS = [s_policy_off, s_potato, s_compass, s_compass_missing, s_errors, s_hostile_cloud,
             s_forget, s_token_not_in_ps, s_socket, s_sources, s_corrupt_file]


def build():
    out = tempfile.mkdtemp(prefix="wishbin-")
    env = dict(os.environ, TEST="1")
    # WISH_ROOT: a copy of the tree with one check removed (tools/wishtest-mutants.py)
    src = os.environ.get("WISH_ROOT", ROOT)
    r = subprocess.run(["sh", os.path.join(src, "src/aurwish/build.sh"), os.path.join(out, "aurwish")],
                       env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        sys.exit(2)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", os.path.join(ROOT, "tools/wishtest-sensorproxy.c"),
                        "-o", os.path.join(out, "sensorproxy")] +
                       subprocess.run(["pkg-config", "--cflags", "--libs", "dbus-1"], capture_output=True, text=True).stdout.split(),
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        sys.exit(2)
    return {"aurwish": os.path.join(out, "aurwish"), "sensorproxy": os.path.join(out, "sensorproxy"), "dir": out}


def main():
    if not shutil.which("dbus-daemon") or not shutil.which("curl"):
        print("wishtest-daemon: needs dbus-daemon and curl; skipped")
        return 0
    bins = build()
    names = sys.argv[1:]
    for fn in SCENARIOS:
        if names and fn.__name__[2:] not in names:
            continue
        print(f"* {fn.__name__[2:]}")
        before = len(fails)
        try:
            fn(bins)
        except Exception as ex:                       # a crash is a failure, with its story
            import traceback
            traceback.print_exc()
            fails.append(f"{fn.__name__} crashed: {ex}")
        for env in list(ENVS):
            env.close()
            if (len(fails) != before or os.environ.get("WISHDEBUG")) and env.last_err.strip():
                print("  [aurwish stderr]", env.last_err.strip()[:2500])
            if len(fails) != before and getattr(env, "curl_err", ""):
                print("  [curl stderr]", env.curl_err.strip()[:1500])
        ENVS.clear()
        if len(fails) == before:
            print("  ok")
    shutil.rmtree(bins["dir"], ignore_errors=True)
    print(f"wishtest-daemon: {len(fails)} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
