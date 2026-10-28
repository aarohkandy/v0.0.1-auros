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

ENGINE_MUTANTS = [
    ("engine: no 3 s of quiet", "engine.c", 'if (e->last_input && now - e->last_input < TYPING_QUIET) return 0;', ''),
    ("engine: holds ignored", "engine.c", 'if (e->holds || e->dnd) return 0;', 'if (e->dnd) return 0;'),
    ("engine: password words queued", "engine.c", '    if (e->holds & H_PASSWORD) return;\n', ''),
    ("engine: sound at night", "engine.c", '            if (wish_do_sound(d->type)) continue;\n', ''),
    ("engine: automatic cards at night", "engine.c",
     'if (wish_do_overlay(d->type) && !(flags & (F_TYPED | F_APP)) && !remind) continue;', ''),
    ("engine: typed cards held at night", "engine.c", '!(flags & (F_TYPED | F_APP)) && !remind', '!remind'),
    ("engine: no per-wish limit", "engine.c", 'if (mine + k > PER_WISH_HOUR ||', 'if (0 ||'),
    ("engine: no overall limit", "engine.c", '(!exempt && all + k > OVERALL_HOUR)', '0'),
    ("engine: paused share kept", "engine.c",
     'if (!strcmp(e->overall[i].wid, s->id)) { e->overall[i].t = 0; e->overall[i].wid[0] = 0; }', ';'),
    ("engine: reminders counted overall", "engine.c",
     'int exempt = w->when.type == W_TIME && w->d[0].type == A_REMIND && w->n == 1;', 'int exempt = 0;'),
    ("engine: never resumes", "engine.c", 'if (s->status == ST_LIMIT && now >= s->paused_until)', 'if (0)'),
    ("engine: no gap between cards", "engine.c",
     'if (has_overlay && s->rt.last_overlay && now - s->rt.last_overlay < OVERLAY_GAP) return;', ''),
    ("engine: unlimited speech", "engine.c", 'if (s->rt.speak_n >= SPEAK_PER_DAY) return;', ''),
    ("engine: sensors never rest", "engine.c",
     'if (!s->rt.last_fire || now - s->rt.last_fire >= SENSOR_GAP) fire(e, s, 0, NULL, now);', 'fire(e, s, 0, NULL, now);'),
    ("engine: compass without hysteresis", "engine.c", 'd > s->w.when.tol + 5', 'd > s->w.when.tol'),
    ("engine: level not edge", "engine.c",
     'if (!s->rt.known) { s->rt.known = 1; s->rt.armed = !in; return; }', 'if (!s->rt.known) { s->rt.known = 1; s->rt.armed = 1; }'),
    ("engine: battery without hysteresis", "engine.c", 'pct >= w->percent + 3', 'pct >= w->percent'),
    ("engine: reminders forever", "engine.c", 'if (remind && now - due <= REMIND_HELD_FOR)', 'if (remind)'),
    ("engine: everything catches up", "engine.c", 'if (remind && now - due <= REMIND_HELD_FOR)', 'if (now - due <= REMIND_HELD_FOR)'),
    ("engine: held for ever", "engine.c", 'now - h->at > life) {', '0) {'),
    ("engine: duplicates allowed", "engine.c", 'if (!oa.bad && !ob.bad && !strcmp(a, b)) return "duplicate";', ''),
    ("engine: unlimited trigger words", "engine.c", '> TRIGGER_WORDS) return "limit";', '> 100) return "limit";'),
    ("engine: unlimited reminders", "engine.c", 'if (n > REMIND_PER_DAY) return "limit";', ''),
    ("engine: offers never expire", "engine.c", '!p->used && now - p->at < PENDING_FOR', '!p->used'),
    ("engine: offers reusable", "engine.c", '!p->used && now - p->at < PENDING_FOR', 'now - p->at < PENDING_FOR'),
    ("engine: forged did accepted", "engine.c", 'if (se->n != nv->i || se->at) return 0;', 'if (se->at) return 0;'),
    ("engine: undo for ever", "engine.c", 'if (!e->nundo || now - e->undo_at >= UNDO_FOR)', 'if (!e->nundo)'),
    ("engine: keep_it into the void", "engine.c", 'if (!o.bad && emit(e, b) > 0)', 'if (!o.bad && (emit(e, b), 1))'),
    ("engine: kept for ever unanswered", "engine.c", '            s->status = ST_OFF;\n            ch = 1;\n', ''),
    ("engine: all_off ignored", "engine.c", 'if (!s->ok || e->st->all_off) return;', 'if (!s->ok) return;'),
    ("engine: prefix words", "engine.c", 'if (s->ok && s->w.when.type == W_TYPED && !strcmp(s->w.when.word, w))',
     'if (s->ok && s->w.when.type == W_TYPED && !strncmp(s->w.when.word, w, strlen(s->w.when.word)))'),
    ("engine: brightness floor at run time", "engine.c", 'if (s) want = clampi(want, 30, 100);', ''),
    ("engine: volume ceiling at run time", "engine.c", 'if (s) want = clampi(want, 20, 80);', ''),
    ("engine: cloud's reason echoed", "engine.c",
     'for (int i = 0; m && MISSING[i]; i++) if (!strcmp(m, MISSING[i])) return MISSING[i];\n    return "other";',
     'return m ? m : "other";'),
    ("engine: invalid alt_wish offered", "engine.c", 'int have = doc && wish_parse(doc, &env, &w, &err) == 0;', 'int have = doc != NULL; if (have) wish_parse(doc, &env, &w, &err);'),
    ("engine: turning off does not forget", "engine.c", 'if (was && !e->st->consent && e->io.forget) e->io.forget(e->io.ctx);', ''),
    ("engine: forget does not forget", "engine.c", '        if (e->io.forget) e->io.forget(e->io.ctx);\n        store_free(e->st);', '        store_free(e->st);'),
    ("engine: camera hold ignored", "engine.c", 'if (e->io.holds && e->io.holds(e->io.ctx)) return 0;', ''),
    ("engine: stopped words still sent", "engine.c",
     "if (e->st->all_off || !s->ok || s->status != ST_ON || s->w.when.type != W_TYPED) continue;",
     "if (!s->ok || s->w.when.type != W_TYPED) continue;"),
]


# The compositor's matcher (src/aurwl/wishhook.c) is tested by tools/wishtest-hook.c.
HOOK_MUTANTS = [
    ("hook: prefix matches (potatoes)", 'if (!strcmp(buf, words[i])) {', 'if (!strncmp(buf, words[i], strlen(words[i]))) {'),
    ("hook: digits end a word (potato1)", '    bad = 1;\n}\n\nvoid wishhook_app', '    end_of_word();\n}\n\nvoid wishhook_app'),
    ("hook: the word is kept after it ends", '    wipe();\n    bad = 0;\n}\n\n/* WHAT A FIELD TELLS US', '    bad = 0;\n}\n\n/* WHAT A FIELD TELLS US'),
    ("hook: looks when nothing is registered", 'if (!nwords || !pressed) return;', 'if (!pressed) return;'),
    ("hook: a moved cursor is trusted", '        wipe();\n        bad = 1;                                   /* the next letters may be mid-word */', '        wipe();'),
    ("hook: every program is reported", 'if (!strcasecmp(app_id, apps[i])) {', 'if (1) {'),
    ("hook: the queue grows without bound", 'if (qw_n < QUEUE) {', 'if (1) {'),
    ("hook: three-letter words", 'int ok = l >= 4 && l <= WORD_MAX;', 'int ok = l >= 1 && l <= WORD_MAX;'),
    ("hook: deleting unseen text is trusted", '        else bad = 1;                              /* deleting something we did not see */', ''),
    ("hook: a new list keeps the old finds", '    qw_n = qw_head = 0;\n    qa_n = qa_head = 0;\n}', '}'),
    ("hook: a window title asking for a password is ignored", 'if (has_any(app, NEVER_APPS) || has_any(aurwl_win_title(w), SECRET_TITLES)) return 1;', 'if (has_any(app, NEVER_APPS)) return 1;'),
    ("hook: password programs are trusted when their field says ordinary", 'if (has_any(app, NEVER_APPS) || has_any(aurwl_win_title(w), SECRET_TITLES)) return 1;', 'if (has_any(aurwl_win_title(w), SECRET_TITLES)) return 1;'),
    ("hook: a secret field is not a reason", '    if (st == TI_SENSITIVE) return 1;\n', ''),
    ("hook: an ordinary field is not trusted", '    if (st == TI_PLAIN) return 0;\n', ''),
    ("hook: a browser with no field is trusted", '    return has_any(app, BROWSER_APPS);', '    return 0;'),
    ("hook: an X program's keys are matched", '    if (aurwl_win_is_x11(w)) return 1;', '    if (0) return 1;'),
    ("hook: a program that will not say who it is is trusted", '    if (!app || !*app) return 1;                  /* a program that will not say who it is */\n', ''),
    ("hook: the half word survives a secret field", '        wipe();\n        bad = 1;\n        was_sensitive = 1;\n        return;', '        was_sensitive = 1;\n        return;'),
    ("hook: Enter does not end a word", '        end_of_word();\n        return;\n    case K_BACKSPACE:', '        return;\n    case K_BACKSPACE:'),
]


# The daemon as a whole: (name, file in src/aurwish, old, new, scenarios that must go red).
DAEMON_MUTANTS = [
    ("daemon: features file ignored", "main.c", '!features_on(OVERRIDE("AURWISH_FEATURES", "/etc/auros/features")) ||', '0 ||', ["policy_off"]),
    ("daemon: admin's wishes = off ignored", "main.c", 'policy_forbids(OVERRIDE("AURWISH_POLICY", "/etc/auros/policy.conf"))))', '0))', ["policy_off"]),
    ("daemon: any odd policy value counts as on", "main.c", 'forbids = strcmp(v, "on") != 0;', 'forbids = strcmp(v, "off") == 0;', ["policy_off"]),
    ("daemon: wishes sent without consent", "main.c", 'if (!ST->consent) {', 'if (0) {', ["potato"]),
    ("daemon: the same words sent twice", "main.c", '(last_text.at && now - last_text.at < 30 && last_text.hash == hs)', '0', ["errors"]),
    # Not here, on purpose: "lines never end" (read() is bounded and a full buffer
    # reads 0, so the explicit length check is a second guard on the same fact) and
    # "token file world-readable" (umask 077 is set at start AND the file is created
    # 0600). Removing either guard alone changes nothing a test can see.
    ("daemon: Forget does not tell the service", "main.c", 'static int io_forget(void *c) { (void)c; if (CL) cloud_forget(CL); return 0; }', 'static int io_forget(void *c) { (void)c; return 0; }', ["forget"]),
    ("cloud: numbers sent as they are", "cloud.c", 'if (digits >= 5) {', 'if (digits >= 500) {', ["errors"]),
    ("cloud: no silent re-registration", "cloud.c", 'if (r.http == 401 && !c->retried_auth) {', 'if (0) {', ["errors"]),
    ("cloud: the token on the command line", "cloud.c", '"--user-agent", "AurOS-Wishes/" VERSION,', '"--user-agent", c->token[0] ? c->token : "AurOS-Wishes/" VERSION,', ["token_not_in_ps"]),
    ("cloud: the token survives Forget", "cloud.c", '    if (c->token[0]) spawn_curl(c, "/v1/forget", "{}", 2, 1, 1);\n    unlink(c->cfg.token_path);', '    if (c->token[0]) spawn_curl(c, "/v1/forget", "{}", 2, 1, 1);', ["forget"]),
    ("cloud: a wrong clock is not corrected", "cloud.c", 'if (r.http == 400 && !c->retried_pow) {', 'if (0) {', ["errors"]),
    ("answer: help pages are any path", "answer.c", 'if (p && p->type == J_STR && page_name_ok(p->s)) {', 'if (p && p->type == J_STR) {', ["hostile_cloud"]),
    ("answer: 429 is an answer", "answer.c", 'if (r->http == 429) { answer_error(r->id, "limit", r->retry_after, reply, n); return; }', '', ["errors"]),
    ("sensors: claimed whether needed or not", "sensors.c", 'mask &= s->have;', 'mask = s->have;', ["compass"]),
    ("wish: a title from the cloud is shown", "wish.c", 'if (!jonly(doc, TOP)) NO("other", "unknown field", "");', '', ["hostile_cloud"]),
]


def daemon_mutants(only):
    bad = 0
    for name, fname, old, new, scenarios in DAEMON_MUTANTS:
        if only and only not in name:
            continue
        d = tempfile.mkdtemp(prefix="wishmut-")
        try:
            for sub in ("src/aurwish", "src/aurshell", "src/common", "build"):
                shutil.copytree(os.path.join(ROOT, sub), os.path.join(d, sub))
            path = os.path.join(d, "src/aurwish", fname)
            text = open(path, encoding="utf-8").read()
            if text.count(old) != 1:
                print(f"STALE {name}: pattern found {text.count(old)} times in {fname}")
                bad += 1
                continue
            open(path, "w", encoding="utf-8").write(text.replace(old, new))
            r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/wishtest-daemon.py")] + scenarios,
                               env=dict(os.environ, WISH_ROOT=d), capture_output=True, text=True, timeout=600)
            if "does not compile" in r.stderr or "error:" in r.stderr[:2000]:
                print(f"BUILD  {name}: the mutant does not compile")
                bad += 1
            elif r.returncode != 0:
                print(f"caught {name}")
            else:
                print(f"MISSED {name}: the {'/'.join(scenarios)} test passes without this check")
                bad += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return bad


# The compositor side of text-input-v3 (src/aurwl/textinput.c), against tools/wishtest-textinput.c.
TI_MUTANTS = [
    ("textinput: password is not a secret", 's->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD ||\n', ''),
    ("textinput: a PIN is not a secret", 's->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN ||\n', ''),
    ("textinput: a terminal is not a secret", 's->purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL ||\n', ''),
    ("textinput: an unknown purpose is safe", 's->purpose > ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL) return 1;', '0) return 1;'),
    ("textinput: hidden_text means nothing",
     '(s->hint & (ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |\n                       ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA)) != 0',
     '(s->hint & ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA) != 0'),
    ("textinput: sensitive_data means nothing",
     '(s->hint & (ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |\n                       ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA)) != 0',
     '(s->hint & ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT) != 0'),
    ("textinput: a second input can enable too",
     '    if (t->cur.enabled) {\n        struct ti *o;\n        wl_list_for_each(o, &tis, link)\n            if (o != t && o->client == t->client && o->entered && o->cur.enabled) {\n                t->cur.enabled = t->pend.enabled = 0;\n                break;\n            }\n    }', ''),
    ("textinput: a destroyed surface is remembered", '    (void)l; (void)data;\n    leave_current(0);', '    (void)l; (void)data;'),
    ("textinput: any number of text inputs", 'if (mine >= MAX_PER_CLIENT || n_total >= MAX_TOTAL) {', 'if (0) {'),
    # not here: "state is reported with nothing focused" is a second guard on a fact leave_current() already makes true
]


def ti_mutants(only):
    bad = 0
    cc = os.environ.get("CC", "cc")
    for name, old, new in TI_MUTANTS:
        if only and only not in name:
            continue
        d = tempfile.mkdtemp(prefix="wishmut-")
        try:
            for sub in ("src/aurwl", "src/aurshell"):
                shutil.copytree(os.path.join(ROOT, sub), os.path.join(d, sub))
            os.makedirs(os.path.join(d, "tools"))
            shutil.copy(os.path.join(ROOT, "tools/wishtest-textinput.c"), os.path.join(d, "tools"))
            path = os.path.join(d, "src/aurwl/textinput.c")
            text = open(path, encoding="utf-8").read()
            if text.count(old) != 1:
                print(f"STALE {name}: pattern found {text.count(old)} times")
                bad += 1
                continue
            open(path, "w", encoding="utf-8").write(text.replace(old, new))
            g = os.path.join(d, "src/aurwl/gen")
            exe = os.path.join(d, "t")
            libs = subprocess.run(["pkg-config", "--cflags", "--libs", "wayland-server", "wayland-client", "xkbcommon"],
                                  capture_output=True, text=True).stdout.split()
            b = subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-w", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                "-I", g, "-I", os.path.join(d, "src/aurwl"), "-o", exe,
                                os.path.join(d, "tools/wishtest-textinput.c"), os.path.join(d, "src/aurwl/aurwl.c"), path,
                                os.path.join(d, "src/aurwl/wishhook.c"), os.path.join(d, "src/aurshell/draw.c")] +
                               [os.path.join(g, f) for f in ("xdg-shell-protocol.c", "xdg-decoration-protocol.c",
                                                              "viewporter-protocol.c", "xdg-output-protocol.c",
                                                              "text-input-protocol.c")] + libs + ["-lm"],
                               capture_output=True)
            if b.returncode:
                print(f"BUILD  {name}: the mutant does not compile")
                bad += 1
                continue
            r = subprocess.run([exe], capture_output=True, timeout=120, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
            if r.returncode != 0:
                print(f"caught {name}")
            else:
                print(f"MISSED {name}: the tests pass without this check")
                bad += 1
        except subprocess.TimeoutExpired:
            print(f"caught {name} (it hangs)")
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return bad


def hook_mutants(only):
    bad = 0
    for name, old, new in HOOK_MUTANTS:
        if only and only not in name:
            continue
        d = tempfile.mkdtemp(prefix="wishmut-")
        try:
            os.makedirs(os.path.join(d, "src/aurwl")); os.makedirs(os.path.join(d, "src/aurshell")); os.makedirs(os.path.join(d, "tools"))
            for f in ("wishhook.c", "wishhook.h", "aurwl.h", "textinput.h"):
                shutil.copy(os.path.join(ROOT, "src/aurwl", f), os.path.join(d, "src/aurwl", f))
            shutil.copy(os.path.join(ROOT, "src/aurshell/draw.h"), os.path.join(d, "src/aurshell/draw.h"))
            shutil.copy(os.path.join(ROOT, "tools/wishtest-hook.c"), os.path.join(d, "tools/wishtest-hook.c"))
            path = os.path.join(d, "src/aurwl/wishhook.c")
            text = open(path, encoding="utf-8").read()
            if text.count(old) != 1:
                print(f"STALE {name}: pattern found {text.count(old)} times")
                bad += 1
                continue
            open(path, "w", encoding="utf-8").write(text.replace(old, new))
            exe = os.path.join(d, "t")
            b = subprocess.run([CC, "-std=gnu11", "-O1", "-g", "-w", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                "-I", os.path.join(d, "src/aurwl"), os.path.join(d, "tools/wishtest-hook.c"), path, "-o", exe],
                               capture_output=True)
            if b.returncode:
                print(f"BUILD  {name}: the mutant does not compile")
                bad += 1
            elif subprocess.run([exe], capture_output=True, timeout=60).returncode != 0:
                print(f"caught {name}")
            else:
                print(f"MISSED {name}: the tests pass without this check")
                bad += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return bad


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
    if "--daemon" in sys.argv and not only:
        pass
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
    if not only or "hook" in only:
        bad += hook_mutants(only)
    if "--textinput" in sys.argv or (only and "textinput:" in only):
        bad += ti_mutants(only)
    if "--daemon" in sys.argv or (only and any(k in only for k in ("daemon", "cloud:", "answer:", "sensors:"))):
        bad += daemon_mutants(only)
    print(f"mutants: {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
