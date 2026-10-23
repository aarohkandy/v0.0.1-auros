#!/usr/bin/env python3
"""NOT PART OF AUROS. The test pages' web server (127.0.0.1) and the place they report to:
POST /<name> writes OUT/<name>.json. Range requests are supported, as a film needs."""
import http.server, os, re, sys

WWW, OUT, PORT = sys.argv[1], sys.argv[2], int(sys.argv[3])


class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=WWW, **k)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)))
        name = re.sub(r"[^A-Za-z0-9_.-]", "", os.path.basename(self.path.split("?")[0])) or "report"
        with open(os.path.join(OUT, name + ".json"), "wb") as f:
            f.write(body)
        self.send_response(204)
        self.end_headers()

    def send_head(self):                      # + byte ranges
        rng = self.headers.get("Range")
        path = self.translate_path(self.path)
        m = re.match(r"bytes=(\d*)-(\d*)$", rng or "")
        if not m or not os.path.isfile(path):
            return super().send_head()
        size = os.path.getsize(path)
        a = int(m.group(1)) if m.group(1) else max(0, size - int(m.group(2)))
        b = min(int(m.group(2)), size - 1) if m.group(1) and m.group(2) else size - 1
        f = open(path, "rb")
        f.seek(a)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", "bytes %d-%d/%d" % (a, b, size))
        self.send_header("Content-Length", str(b - a + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        self._left = b - a + 1
        return f

    def copyfile(self, src, dst):
        n = getattr(self, "_left", None)
        self._left = None
        while True:
            chunk = src.read(65536 if n is None else min(65536, n))
            if not chunk:
                break
            dst.write(chunk)
            if n is not None:
                n -= len(chunk)
                if n <= 0:
                    break

    def log_message(self, *a):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
