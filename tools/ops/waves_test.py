#!/usr/bin/env python3
"""python3 -I tools/ops/waves_test.py : the stop rules, each shown to trip and to hold; the commands, against a fake edge."""
import http.server, json, os, sys, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import waves

ok = True
def check(what, cond):
    global ok
    print(("ok   " if cond else "FAIL ") + what); ok = ok and bool(cond)

base = dict(suspected_data_loss=0, shrunk=40, failed_after_shrink=0, completed=40, putback=0)
ev = lambda **kw: waves.evaluate({**base, **kw})

check("all quiet: no reason", ev() == [])
check("one suspected data loss pauses", len(ev(suspected_data_loss=1)) == 1)
check("failure after the shrink at 2% (1 of 50) holds", ev(shrunk=50, failed_after_shrink=1) == [])
check("failure after the shrink above 2% (2 of 50) pauses", len(ev(shrunk=50, failed_after_shrink=2)) == 1)
check("... and 1 of 25 on the first day pauses", len(ev(shrunk=25, failed_after_shrink=1)) == 1)
check("put-back at 15% (6 of 40) holds", ev(putback=6) == [])
check("put-back above 15% (7 of 40) pauses", len(ev(putback=7)) == 1)
check("put-back means nothing with fewer than 5 finished (2 of 4)", ev(shrunk=4, completed=4, putback=2) == [])
check("two rules at once give two reasons", len(ev(suspected_data_loss=2, putback=20)) == 2)
for bad in ({}, {**base, "putback": -1}, {**base, "putback": 41}, {**base, "shrunk": "40"}, {**base, "failed_after_shrink": 99},
            {**base, "completed": True}, None, [], {**base, "suspected_data_loss": None}):
    check(f"unreadable numbers keep the gate shut: {str(bad)[:50]}", waves.evaluate(bad) == [waves.UNREADABLE])

# the commands, against a fake edge that records what it is asked
log = []
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, code, obj):
        b = json.dumps(obj).encode(); self.send_response(code); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        log.append(("GET", self.path, None)); self.reply(200, {"summary": 1, **summary} if self.path == "/summary" else {"open": True})
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("content-length", 0))) or b"{}")
        log.append(("POST", self.path, self.headers.get("authorization"), body)); self.reply(200, {"paused": True})
summary = dict(base)
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
os.environ.update(EDGE_URL=f"http://127.0.0.1:{srv.server_port}", OUTCOMES_URL=f"http://127.0.0.1:{srv.server_port}/summary", OPS_TOKEN="o", HALT_TOKEN="")
os.environ.pop("AUROS_DOMAIN", None)
import io, contextlib
def run(*a):
    log.clear()
    with contextlib.redirect_stdout(io.StringIO()):
        return waves.main(list(a))

check("stop-rules with quiet numbers posts nothing", run("stop-rules") == 0 and not [l for l in log if l[0] == "POST"])
summary.update(suspected_data_loss=1)
rc = run("stop-rules")
posts = [l for l in log if l[0] == "POST"]
check("stop-rules with a data-loss report pauses, once, with the reason", rc == 0 and len(posts) == 1 and posts[0][1] == "/_ops/pause" and "data loss" in posts[0][3]["reason"] and posts[0][2] == "Bearer o")
os.environ["EDGE_URL"] = "http://127.0.0.1:1"; os.environ["OUTCOMES_URL"] = "http://127.0.0.1:1/x"
check("an unreachable summary is not a reason to pause (exit 2, nothing posted)", run("stop-rules") == 2 and not log)
os.environ["EDGE_URL"] = f"http://127.0.0.1:{srv.server_port}"
os.environ["OUTCOMES_URL"] = f"http://127.0.0.1:{srv.server_port}/summary"
live = os.path.join(os.environ.get("TMPDIR", "/tmp"), "live-test.txt")
open(live, "w").write("wishes\n")
orig = waves.live_names; waves.live_names = lambda: orig(live)
check("wave 1 refuses while the monitor is not watching everything", run("wave", "1") == 1 and not [l for l in log if l[0] == "POST"])
open(live, "w").write("site download pieces channel wishes\n")
check("wave 1 opens once all five are live", run("wave", "1") == 0 and log[-1][1] == "/_ops/wave" and log[-1][3] == {"wave": 1})
check("wave 0 (closing) never needs that", (open(live, "w").write("") or run("wave", "0")) == 0)
os.remove(live); waves.live_names = orig
os.environ.pop("OPS_TOKEN"); os.environ["HALT_TOKEN"] = "h"
check("the second responder's token pauses", run("pause", "odd noise") == 0 and log[-1][2] == "Bearer h" and log[-1][3]["reason"] == "odd noise")
try:
    run("resume"); check("... and cannot resume (the command needs the owner's token)", False)
except SystemExit as e:
    check("... and cannot resume (the command needs the owner's token)", "OPS_TOKEN" in str(e))
srv.shutdown()
sys.exit(0 if ok else 1)
