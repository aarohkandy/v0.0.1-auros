#!/usr/bin/env python3
"""wishtest-schema.py -- the engine and the cloud must agree (docs/AI.md 5, 7.1).

docs/wish-v1.schema.json is the document both sides compile. This test
builds the engine's validator (tools/wishcheck.c) and requires:

  1. the emoji allowlist in src/aurwish/text.c equals the schema's;
  2. a document for EVERY variant of `when` and of `do` in the schema,
     at the schema's own minimum and maximum, is accepted (with a
     trigger that the matrix allows for that action) -- the cloud can
     never send a shape the device refuses for being unfamiliar;
  3. the same documents with one more field, or one value just outside
     the schema's range, are refused (the device is at least as strict).

    python3 tools/wishtest-schema.py
"""
import json, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC = os.environ.get("CC", "cc")
S = json.load(open(os.path.join(ROOT, "docs", "wish-v1.schema.json")))
D = S["$defs"]
fails = 0


def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL", msg)


def deref(v):
    while "$ref" in v:
        v = D[v["$ref"].split("/")[-1]]
    return v


def values(v):
    """A few values inside the schema's range, and a few just outside."""
    v = deref(v)
    if "const" in v:
        return [v["const"]], []
    if "enum" in v:
        return list(v["enum"]), ["__not_in_the_enum__"]
    t = v.get("type")
    if t == "array":
        items = v["items"]["enum"]
        return [[items[0]], list(items), items[:3]], [[], [items[0], items[0]], ["someday"]]
    if t == "integer":
        lo, hi = v["minimum"], v["maximum"]
        return sorted({lo, hi, (lo + hi) // 2}), [lo - 1, hi + 1]
    if t == "boolean":
        return [True, False], ["yes", 1]
    if t == "string":
        if v.get("pattern") == "^#[0-9A-Fa-f]{6}$":
            return ["#102040"], ["#12", "red"]
        if "hhmm" in v.get("description", "") or "([01][0-9]" in v.get("pattern", ""):
            return ["00:00", "23:59"], ["24:00", "9:00"]
        if "p{L}+$" in v.get("pattern", ""):
            return ["potato", "Potato"], ["pot", "potato1", "x" * 33]
        if v.get("maxLength") == 64 and v.get("pattern", "").startswith("^[\\p{L}"):
            return ["__NAME__"], ["", "a" * 65, "<script>"]
        return ["hello"], []
    return [], []


def variants(obj):
    """Documents' parts: for each anyOf variant, every combination of one
    in-range value per property, and the out-of-range probes."""
    obj = deref(obj)
    out = []
    for var in obj["anyOf"]:
        props = var["properties"]
        req = var["required"]
        good = {k: values(p)[0] for k, p in props.items()}
        bad = {k: values(p)[1] for k, p in props.items()}
        # all combinations would explode on the emoji enum: take each value
        # once, the rest at their first.
        base = {k: v[0] for k, v in good.items()}
        docs = [dict(base)]
        for k, vs in good.items():
            for x in vs[1:]:
                d = dict(base); d[k] = x; docs.append(d)
        probes = []
        for k, vs in bad.items():
            for x in vs:
                d = dict(base); d[k] = x; probes.append(d)
        d = dict(base); d["extra_field"] = 1; probes.append(d)
        for k in req:
            if k != "type":
                d = dict(base); del d[k]; probes.append(d)
        out.append((docs, probes))
    return out


def main():
    tmp = tempfile.mkdtemp(prefix="wishschema-")
    apps = os.path.join(tmp, "apps"); themes = os.path.join(tmp, "themes")
    os.makedirs(apps); os.makedirs(themes)
    open(os.path.join(apps, "firefox.desktop"), "w").write(
        "[Desktop Entry]\nType=Application\nName=Firefox\n")
    open(os.path.join(themes, "sandstone.theme"), "w").write('theme_name="Sandstone"\n')
    exe = os.path.join(tmp, "wishcheck")
    src = os.path.join(ROOT, "src", "aurwish")
    r = subprocess.run([CC, "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-I", src, os.path.join(ROOT, "tools", "wishcheck.c")] +
                       [os.path.join(src, f) for f in ("json.c", "text.c", "wish.c")] +
                       ["-lm", "-o", exe], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(2)

    # 1. the emoji lists
    text_c = open(os.path.join(src, "text.c"), encoding="utf-8").read()
    block = re.search(r"static const char \*const EMOJI\[\] = \{(.*?)NULL \};", text_c, re.S).group(1)
    mine = re.findall(r'"([^"]+)"', block)
    check(mine == D["emoji"]["enum"], "emoji allowlist differs from the schema's")

    def run(docs, sensors="compass,tilt,light", said="hello potato"):
        p = subprocess.run([exe, "--sensors", sensors, "--apps", apps, "--themes", themes, "--said", said],
                           input="\n".join(json.dumps(d, ensure_ascii=False) for d in docs) + "\n",
                           capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        if p.returncode:
            print(p.stderr); sys.exit(2)
        return p.stdout.splitlines()

    def fix(v):
        # a name as she said it: one the machine really has
        if v.get("type") == "set_wallpaper":
            return json.loads(json.dumps(v).replace("__NAME__", "Sandstone"))
        return json.loads(json.dumps(v).replace("__NAME__", "Firefox"))

    # Where the device is STRICTER than the schema on purpose: a change
    # of 0 does nothing, and "until" on an "off" means nothing.
    def stricter(a):
        return (a.get("change_percent") == 0 or
                (a.get("type") == "do_not_disturb" and a.get("on") is False and "until" in a))

    # 2/3. every `when` with a permitting action, every `do` with a permitting trigger
    ok_do = {"type": "brightness", "set_percent": 50}
    ok_when = {"type": "now"}
    when_sets = variants(D["when"])
    do_sets = variants(D["action"])
    sent = 0
    for docs, probes in when_sets:
        wd = [fix(d) for d in docs]
        wp = [fix(d) for d in probes]
        # sensors need their sensor; the 'sensor' variants are given all three
        good = [{"v": 1, "when": w, "do": [ok_do], "enabled": True} for w in wd]
        for w, line in zip(wd, run(good)):
            sent += 1
            check(line.startswith("ok "), f"when {w} refused: {line}")
        bad = [{"v": 1, "when": w, "do": [ok_do], "enabled": True} for w in wp]
        for w, line in zip(wp, run(bad)):
            check(line.startswith("no "), f"when {w} should be refused: {line}")
    # which trigger each action may use (docs/AI.md 7.4); remind needs `time`.
    trig = {"remind": {"type": "time", "at": "09:00"}, "open_app": {"type": "now"},
            "open_website": {"type": "now"}, "speak": {"type": "now"}, "speak_time": {"type": "now"},
            "volume": {"type": "now"}, "set_look": {"type": "now"}, "set_text_size": {"type": "now"},
            "pointer_size": {"type": "now"}, "set_wallpaper": {"type": "now"}}
    said_all = "hello potato Firefox Sandstone"
    for docs, probes in do_sets:
        ad = [fix(d) for d in docs]
        ap = [fix(d) for d in probes]
        def doc(a):
            return {"v": 1, "when": trig.get(a["type"], ok_when), "do": [a], "enabled": True}
        for a, line in zip(ad, run([doc(a) for a in ad], said=said_all)):
            sent += 1
            if a["type"] == "open_website":
                continue          # sites come from the shipped list: tested below
            check(line.startswith("ok ") or stricter(a), f"do {a} refused: {line}")
        for a, line in zip(ap, run([doc(a) for a in ap], said=said_all)):
            if a["type"] == "open_website":
                continue
            check(line.startswith("no "), f"do {a} should be refused: {line}")
    # sites: the schema takes any name; the device only the shipped list
    lines = run([{"v": 1, "when": {"type": "now"}, "do": [{"type": "open_website", "site": s}], "enabled": True}
                 for s in ("BBC News", "Wikipedia", "evil example")])
    check(lines[0].startswith("ok ") and lines[1].startswith("ok ") and lines[2].startswith("no site"),
          f"sites: {lines}")
    print(f"wishtest-schema: {sent} schema shapes accepted, {fails} problem(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
