#!/usr/bin/env python3
"""wishtest-mutants.py -- prove every Wishes check can fail.

For each mutant below: copy src/aurwish to a scratch directory, remove ONE
check (an exact text replacement that must match exactly once, so a
mutant cannot silently stop applying when the code moves), rebuild the
tests against the copy and require them to FAIL. A mutant the tests do
not catch is a check nothing proves, and this script exits non-zero.

    python3 tools/wishtest-mutants.py [--only NAME]
"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC = os.environ.get("CC", "cc")

# (name, file, old, new). Several pairs may be given as a list.
MUTANTS = [
    ("json: duplicate keys allowed", "json.c",
     'if (!strcmp(k->key, key)) { bad(x, "duplicate key"); return NULL; }', ';'),
    ("json: no depth limit", "json.c",
     [('if (depth > x->lim->max_depth) { bad(x, "nested too deeply"); return NULL; }', ';'),
      ('if (depth + 1 > x->lim->max_depth) { bad(x, "nested too deeply"); return NULL; }', ';')]),
    ("json: controls allowed", "json.c",
     'if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return 0;', ''),
    ("json: overlong UTF-8 allowed", "json.c", 'if (c < min) return -1;                                  /* overlong */', ''),
    ("json: trailing bytes allowed", "json.c",
     'if (x.p != x.end) { a->err = "something after the value"; return NULL; }', ''),
    ("json: size limit off", "json.c",
     'if (len > lim->max_bytes) { a->err = "too big"; return NULL; }', ''),
    ("json: string limit off", "json.c",
     'if (n + 4 > max) { bad(x, "string too long"); return NULL; }', 'if (n + 4 > max + 100000) { bad(x, "x"); return NULL; }'),
    ("text: strchr truncation back", "text.c", '(c && c < 0x80 && strchr(', '(c && strchr('),
    ("text: not-her-words off", "text.c", 'if (!found) return "not her words";', ''),
    ("text: blocked words off", "text.c", 'if (blocked(w)) return "blocked word";', ''),
    ("text: long numbers allowed", "text.c", 'if (++run >= 5) return "long number";', '++run;'),
    ("text: addresses allowed", "text.c", 'return "looks like an address";\n\n', ';\n\n'),
    ("text: money amounts allowed", "text.c", 'if (in_list(AMOUNT, w)) return "money amount";', ''),
    ("text: mixed scripts allowed", "text.c", 'if (k && sc && k != sc) return -1;', ''),
    ("text: stacked accents allowed", "text.c", 'if (++marks > 1) return', 'if (++marks > 9) return'),
    ("text: any emoji", "text.c", '    if (!e) return NULL;\n    for (int i = 0; EMOJI[i]', '    if (e && *e) return e;\n    for (int i = 0; EMOJI[i]'),
    ("text: stop words allowed", "text.c", 'if (in_list(STOP, out)) return "too common";', ''),
    ("text: login words allowed", "text.c",
     'if (in_list(AMOUNT, out) || in_list(MONEY, out) || in_list(LOGIN, out) || blocked(out))\n        return "money or login word";', ''),
    ("text: short trigger words", "text.c", 'if (l < 4) return "shorter than 4 letters";', ''),
    ("text: bidi in labels", "text.c", '(c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) ||', '0 ||'),
    ("wish: unknown top-level fields", "wish.c", 'if (!jonly(doc, TOP)) NO("other", "unknown field", "");', ''),
    ("wish: unknown action fields", "wish.c", 'if (!jonly(o, DO_KEYS[d->type])) NO("other", "unknown field", where);', ''),
    ("wish: install not named", "wish.c",
     'if (t && !strcmp(t, "install_program")) NO("install", "installing is not a wish in v1", where);', ''),
    ("wish: matrix off", "wish.c",
     'if (!(ALLOWED[d->type] & B(w->when.type))) NO("other", "action not allowed with this trigger", where);', ''),
    ("wish: change_percent on triggers", "wish.c",
     'if ((d->type == A_BRIGHT || d->type == A_VOLUME) && !d->is_set && w->when.type != W_NOW)', 'if (0)'),
    ("wish: two outward actions", "wish.c", '++outward > 1', '++outward > 2'),
    ("wish: quiet hours off", "wish.c", 'if (w->when.type == W_TIME && quiet_hour(w->when.hh) &&', 'if (0 &&'),
    ("wish: sound every 5 minutes", "wish.c", 'if (w->when.type == W_EVERY && sound && w->when.minutes < 30)', 'if (0)'),
    ("wish: brightness floor gone", "wish.c", 'pct(o, 30, 100, d, err, where)', 'pct(o, 0, 100, d, err, where)'),
    ("wish: volume over 80", "wish.c", 'pct(o, 20, 80, d, err, where)', 'pct(o, 0, 100, d, err, where)'),
    ("wish: sensor presence ignored", "wish.c", 'if (!(env->sensors & (1u << w->sensor)))', 'if (0)'),
    ("wish: ambiguous program", "wish.c",
     'if (found && strcmp(out->id, id)) { closedir(dh); return -2; }', ''),
    ("wish: program names unchecked", "wish.c", 'if (!text_label_ok(name, 40)) continue;', ''),
    ("wish: no contrast check", "wish.c", 'if (contrast(d->color, env->fg) < 4.5)', 'if (contrast(d->color, env->fg) < 0.0)'),
    ("wish: any site", "wish.c", 'if (site_find(s, &name, &url) < 0) NO("site", "not a site on the list", where);',
     'if (site_find(s, &name, &url) < 0) { name = s; url = s; }'),
    ("wish: unterminated name table", "wish.c",
     [('"pointer_size", "do_not_disturb", NULL };', '"pointer_size", "do_not_disturb" };'),
      ('DO_NAMES[A_N + 1]', 'DO_NAMES[A_N]')]),
    ("store: world-readable", "store.c", 'O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)',
     'O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644)'),
    ("store: unreadable file left in place", "store.c", '        rename(path, aside);\n', ''),
    ("store: newer wishes dropped", "store.c",
     'if (v && v->type == J_INT && v->i > 1) { s->status = ST_NEWER; return s; }', ''),
    ("store: follows symlinks", "store.c", 'O_RDONLY | O_CLOEXEC | O_NOFOLLOW', 'O_RDONLY | O_CLOEXEC'),
    ("store: duplicate ids kept", "store.c", 'if (!s->id[0] || dup) { idn = 0; s->id[0] = 0; }', ''),
    ("store: tampered text runs", "store.c", 'all.said = s->said;', 'all.said = s->raw;'),
]

ENGINE_MUTANTS = []   # filled by tools/wishtest-engine.c's own list (below)


def build_and_run(srcdir, test, extra):
    out = os.path.join(srcdir, "t")
    # Sanitizers on: a mutant that reads past a table must be caught
    # even when the bytes it reads happen not to crash.
    cmd = [CC, "-std=gnu11", "-O1", "-g", "-w", "-fsanitize=address,undefined",
           "-fno-sanitize-recover=all", "-I", srcdir, os.path.join(ROOT, "tools", test)] + \
          [os.path.join(srcdir, f) for f in extra] + ["-lm", "-o", out]
    if subprocess.run(cmd, capture_output=True).returncode != 0:
        return "build"
    env = dict(os.environ, TMPDIR=srcdir)
    r = subprocess.run([out], capture_output=True, env=env, timeout=120)
    return "pass" if r.returncode == 0 else "fail"


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    src = os.path.join(ROOT, "src", "aurwish")
    tests = [("wishtest.c", ["json.c", "text.c", "wish.c", "store.c"])]
    if os.path.exists(os.path.join(ROOT, "tools", "wishtest-engine.c")):
        tests.append(("wishtest-engine.c", ["json.c", "text.c", "wish.c", "store.c", "engine.c"]))
    bad = 0
    for name, fname, *rest in MUTANTS + ENGINE_MUTANTS:
        if only and only not in name:
            continue
        pairs = rest[0] if isinstance(rest[0], list) else [(rest[0], rest[1])]
        d = tempfile.mkdtemp(prefix="wishmut-")
        try:
            for f in os.listdir(src):
                shutil.copy(os.path.join(src, f), d)
            path = os.path.join(d, fname)
            text = open(path, encoding="utf-8").read()
            for old, new in pairs:
                n = text.count(old)
                if n != 1:
                    print(f"STALE {name}: pattern found {n} times in {fname}")
                    bad += 1
                    break
                text = text.replace(old, new)
            else:
                open(path, "w", encoding="utf-8").write(text)
                results = [build_and_run(d, t, srcs) for t, srcs in tests]
                if "fail" in results:
                    print(f"caught {name}")
                elif "build" in results:
                    print(f"BUILD  {name}: the mutant does not compile")
                    bad += 1
                else:
                    print(f"MISSED {name}: the tests pass without this check")
                    bad += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print(f"mutants: {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
