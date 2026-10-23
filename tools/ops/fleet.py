#!/usr/bin/env python3
"""fleet.csv: one row a day of how many people took AurOS (WP-35). .github/workflows/fleet.yml runs it daily.

    python3 -I tools/ops/fleet.py [--selftest | OLD.csv > NEW.csv]

Counts only what is public and personal to nobody: GitHub's own download counter on the installer of
every full (non-pre) release, and, once the owner's domain and gate exist, yesterday's whole-day count at the gate and
the wave. A day already in the file is replaced, not repeated. The file is kept as an asset of the
prerelease tag `fleet`; nothing is committed.
"""
import csv, io, json, os, subprocess, sys, time, urllib.request

COLS = ["date", "installer_downloads_total", "latest_tag", "latest_installer_downloads", "gate_wave", "gate_yesterday"]


def gh_json(*a):
    return json.loads(subprocess.run(["gh", *a], check=True, capture_output=True, text=True).stdout)


def collect(repo, edge=None, today=None):
    rels = gh_json("api", f"repos/{repo}/releases?per_page=100", "--paginate", "--slurp")
    rels = [r for page in rels for r in page] if rels and isinstance(rels[0], list) else rels
    full = [r for r in rels if not r.get("prerelease") and not r.get("draft")]
    dl = lambda r: sum(a.get("download_count", 0) for a in r.get("assets", []) if a["name"] == "AurOS-Setup.exe")
    latest = full[0] if full else None
    wave = yday = ""
    if edge:
        try:
            with urllib.request.urlopen(edge.rstrip("/") + "/_ops/state", timeout=20) as r:
                wave = json.load(r).get("wave", "")
            with urllib.request.urlopen(edge.rstrip("/") + "/_ops/history", timeout=20) as r:
                yday = json.load(r).get(time.strftime("%Y-%m-%d", time.gmtime(time.time() - 86400)), 0)  # a whole day, not this morning's part of one
        except Exception:
            pass  # the gate being unreachable is the monitor's business; the row just has blanks
    return {"date": today or time.strftime("%Y-%m-%d", time.gmtime()), "installer_downloads_total": sum(dl(r) for r in full),
            "latest_tag": latest["tag_name"] if latest else "", "latest_installer_downloads": dl(latest) if latest else 0,
            "gate_wave": wave, "gate_yesterday": yday}


def merge(old_text, row):
    rows = [r for r in csv.DictReader(io.StringIO(old_text or "")) if r.get("date") != row["date"]]
    rows.append({k: str(v) for k, v in row.items()})
    rows.sort(key=lambda r: r["date"])
    out = io.StringIO()
    w = csv.DictWriter(out, COLS, lineterminator="\n", extrasaction="ignore")
    w.writeheader(); w.writerows(rows)
    return out.getvalue()


def selftest():
    ok = True
    def check(w, c):
        nonlocal ok
        print(("ok   " if c else "FAIL ") + w); ok = ok and bool(c)
    a = merge("", {"date": "2026-11-01", "installer_downloads_total": 3, "latest_tag": "v1", "latest_installer_downloads": 3, "gate_wave": 1, "gate_yesterday": 2})
    check("first row gets a header", a.splitlines()[0] == ",".join(COLS) and len(a.splitlines()) == 2)
    b = merge(a, {"date": "2026-11-02", "installer_downloads_total": 9, "latest_tag": "v1", "latest_installer_downloads": 9, "gate_wave": "", "gate_yesterday": ""})
    check("a second day is appended in date order", [l.split(",")[0] for l in b.splitlines()[1:]] == ["2026-11-01", "2026-11-02"])
    c = merge(b, {"date": "2026-11-01", "installer_downloads_total": 5, "latest_tag": "v1", "latest_installer_downloads": 5, "gate_wave": 1, "gate_yesterday": 4})
    check("the same day again replaces, not repeats", len(c.splitlines()) == 3 and ",5,v1,5,1,4" in c)
    check("a stray column in the old file is dropped, not carried", "x" not in merge("date,x\n2026-10-01,x\n", {"date": "2026-10-02", "installer_downloads_total": 0, "latest_tag": "", "latest_installer_downloads": 0, "gate_wave": "", "gate_yesterday": ""}).splitlines()[0])
    return 0 if ok else 1


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    repo = os.environ["GITHUB_REPOSITORY"]
    dom = os.environ.get("AUROS_DOMAIN", "").strip()
    old = open(sys.argv[1]).read() if len(sys.argv) > 1 and os.path.exists(sys.argv[1]) else ""
    sys.stdout.write(merge(old, collect(repo, f"https://get.{dom}" if dom else None)))
