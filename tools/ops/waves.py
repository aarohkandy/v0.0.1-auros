#!/usr/bin/env python3
"""Launch waves and the automatic stop rules (WP-74). Off until the owner opens wave 1.

    python3 -I tools/ops/waves.py state                 who may download today (public)
    python3 -I tools/ops/waves.py pause "why"           stop downloads  (owner or second responder)
    python3 -I tools/ops/waves.py resume                 start them again (owner only)
    python3 -I tools/ops/waves.py wave N                 0 closed, 1 about 25 a day, 2 100, 3 300, 4 open (owner only)
    python3 -I tools/ops/waves.py stop-rules             read the field numbers, pause if a rule trips
    python3 -I tools/ops/waves.py post-status FILE       give the status page the monitor's result (owner only)

Tokens (Worker secrets; GitHub secrets AUROS_OPS_TOKEN / AUROS_HALT_TOKEN): the environment variable
OPS_TOKEN is the owner's; HALT_TOKEN is the second responder's and can only pause.
The edge Worker is https://get.<AUROS_DOMAIN> (EDGE_URL overrides). docs/ops/LAUNCH.md has the rules and the routine.
"""
import json, os, sys, urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# The stop rules (PLAN-END-TO-END decision 21): any one pauses downloads by itself.
MAX_FAILED_AFTER_SHRINK = 0.02  # of the PCs whose Windows was shrunk
MAX_PUTBACK = 0.15              # of the installs that finished; with fewer than MIN_FINISHED it means nothing
MIN_FINISHED = 5
UNREADABLE = "the field numbers cannot be read (a missing or impossible count)"
LIVE_NEEDED = {"site", "download", "pieces", "channel", "wishes"}


def evaluate(s):
    """The field summary -> the reasons to pause (empty = carry on).

    s = {"suspected_data_loss", "shrunk", "failed_after_shrink", "completed", "putback"}: counts for the
    trailing 7 days (docs/ops/LAUNCH.md has the shape the report Worker serves). A number that is not a
    count is itself a reason: a gate that cannot read its instruments stays shut."""
    keys = ["suspected_data_loss", "shrunk", "failed_after_shrink", "completed", "putback"]
    try:
        n = {k: s[k] for k in keys}
        if not all(isinstance(v, int) and not isinstance(v, bool) and v >= 0 for v in n.values()):
            raise ValueError
        if n["failed_after_shrink"] > n["shrunk"] or n["putback"] > n["completed"]:
            raise ValueError
    except (KeyError, TypeError, ValueError):
        return [UNREADABLE]
    why = []
    if n["suspected_data_loss"] >= 1:
        why.append(f"suspected data loss reported ({n['suspected_data_loss']})")
    if n["shrunk"] and n["failed_after_shrink"] / n["shrunk"] > MAX_FAILED_AFTER_SHRINK:
        why.append(f"{n['failed_after_shrink']} of {n['shrunk']} failed after Windows was shrunk (limit {MAX_FAILED_AFTER_SHRINK:.0%})")
    if n["completed"] >= MIN_FINISHED and n["putback"] / n["completed"] > MAX_PUTBACK:
        why.append(f"{n['putback']} of {n['completed']} put Windows back (limit {MAX_PUTBACK:.0%})")
    return why


def live_names(path=None):
    path = path or os.path.join(ROOT, "docs/ops/live.txt")
    try:
        return {w for l in open(path) for w in l.split("#")[0].split()}
    except OSError:
        return set()


def edge():
    url = os.environ.get("EDGE_URL") or (f"https://get.{os.environ['AUROS_DOMAIN'].strip()}" if os.environ.get("AUROS_DOMAIN", "").strip() else "")
    if not url:
        raise SystemExit("waves: no edge address. Set AUROS_DOMAIN (the owner's domain, docs/ops/DOMAIN.md); until then there is no gate to operate.")
    return url.rstrip("/")


def call(path, body=None, token=None, method=None):
    req = urllib.request.Request(edge() + path, data=None if body is None else json.dumps(body).encode(),
                                 method=method or ("GET" if body is None else "POST"),
                                 headers={"content-type": "application/json", **({"authorization": f"Bearer {token}"} if token else {})})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, {"error": e.read()[:200].decode("utf-8", "replace")}


def need(token_names):
    for n in token_names:
        if os.environ.get(n):
            return os.environ[n]
    raise SystemExit(f"waves: this needs the environment variable {' or '.join(token_names)}")


def show(code, obj):
    print(json.dumps(obj, indent=1))
    return 0 if code == 200 else 1


def main(argv):
    cmd, rest = (argv or ["help"])[0], argv[1:]
    if cmd == "state":
        return show(*call("/_ops/state"))
    if cmd == "pause":
        return show(*call("/_ops/pause", {"reason": " ".join(rest) or "paused by hand"}, need(["OPS_TOKEN", "HALT_TOKEN"])))
    if cmd == "resume":
        return show(*call("/_ops/resume", {}, need(["OPS_TOKEN"])))
    if cmd == "wave" and rest and rest[0].isdigit():
        n = int(rest[0])
        if n >= 1:
            missing = sorted(LIVE_NEEDED - live_names())
            if missing:
                print(f"waves: wave {n} stays shut: docs/ops/live.txt does not yet list {', '.join(missing)}. "
                      "Those are the things the monitor must be watching before a stranger can be sent here.")
                return 1
        return show(*call("/_ops/wave", {"wave": n}, need(["OPS_TOKEN"])))
    if cmd == "post-status" and rest:
        return show(*call("/_ops/status", json.load(open(rest[0])), need(["OPS_TOKEN"])))
    if cmd == "stop-rules":
        src = os.environ.get("OUTCOMES_URL")
        if not src:
            print("waves: stop-rules: no OUTCOMES_URL (the field numbers, WP-73, are not being collected yet); nothing to read")
            return 0
        req = urllib.request.Request(src, headers={"authorization": f"Bearer {os.environ.get('OPS_TOKEN', '')}"})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                summary = json.loads(r.read())
        except urllib.error.HTTPError as e:
            summary = None if e.code >= 500 else {}  # a 4xx is reachable but unreadable
            if summary is None:
                print(f"waves: stop-rules: the field numbers answered {e.code}; not pausing on an outage (the monitor reports it)")
                return 2
        except (urllib.error.URLError, OSError, ValueError) as e:
            print(f"waves: stop-rules: could not read the field numbers ({e}); not pausing on an outage")
            return 2
        why = evaluate(summary)
        if not why:
            print("waves: stop-rules: inside every rule")
            return 0
        reason = "; ".join(why)
        print(f"waves: stop-rules TRIPPED: {reason}")
        code, obj = call("/_ops/pause", {"reason": "stop rule: " + reason}, need(["OPS_TOKEN", "HALT_TOKEN"]))
        print(json.dumps(obj))
        return 0 if code == 200 else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
