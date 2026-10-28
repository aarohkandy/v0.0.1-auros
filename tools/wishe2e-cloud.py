#!/usr/bin/env python3
"""A stand-in for the Wishes service, for tools/wishe2e.sh (never for anything else).

It accepts any registration and answers /v1/wish from a fixed table, so the
test can see the whole chain: the panel, the socket, the engine, the engine's
own checks, and back. The real service is cloud/ (stream CL); what its answers
look like is docs/AI.md section 5.

  python3 tools/wishe2e-cloud.py PORT SERVICE_LOG [READY_FILE]
"""
import json, os, sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

POTATO = {"v": 1, "when": {"type": "typed_word", "word": "potato"},
          "do": [{"type": "fill_screen", "emoji": "\U0001F954", "seconds": 3}]}
BIGGER = {"v": 1, "when": {"type": "now"}, "do": [{"type": "set_text_size", "size": "bigger"}]}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(n) or b"{}")
        if self.path == "/v1/register":
            return self.send(200, {"token": "t" * 40, "expires_at": int(time.time()) + 80 * 86400})
        if self.path == "/v1/forget":
            return self.send(200, {})
        if self.path == "/v1/wish":
            text = (body.get("text") or "").lower()
            with open(sys.argv[2], "a") as f:      # what the service was sent: counted, never kept in real life
                f.write(json.dumps(sorted(body.keys())) + "\n")
            if "potato" in text:
                return self.send(200, {"kind": "wish", "wish": POTATO})
            if "bigger" in text:
                return self.send(200, {"kind": "wish", "wish": BIGGER})
            if "compass" in text:
                return self.send(200, {"kind": "cannot", "missing": "compass"})
            if "busy" in text:
                return self.send(503, {"text": "busy"})
            return self.send(200, {"kind": "cannot", "missing": "other"})
        self.send(404, {})

# Binding the socket is part of starting the stand-in.  Tell the launcher only
# after that has happened: a spawned Python process is not yet a listening
# HTTP server.  The short delay is a test hook used by wishe2e.sh to keep that
# ordering covered instead of relying on the scheduler.
time.sleep(float(os.environ.get("WISHE2E_CLOUD_START_DELAY", "0")))
server = HTTPServer(("127.0.0.1", int(sys.argv[1])), H)
if len(sys.argv) > 3:
    with open(sys.argv[3], "w") as ready:
        ready.write(f"{server.server_port}\n")
server.serve_forever()
