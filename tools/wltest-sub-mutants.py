#!/usr/bin/env python3
"""wltest-sub-mutants.py -- prove every subsurface check can fail.

For each mutant below: copy src/aurwl/aurwl.c, break ONE thing in it (an
exact text replacement that must match exactly once, so a mutant cannot
silently stop applying when the code moves), build tools/wltest-sub.c
against the copy and require the named scenario to FAIL. A mutant the
test does not catch is a behaviour nothing proves, and this script exits
non-zero.

    python3 tools/wltest-sub-mutants.py [--only SUBSTRING]

Needs what tools/wltest-sub.sh needs (Linux, libwayland-dev, cc).
"""
import os, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "aurwl", "aurwl.c")

# (what is broken, scenario that must notice, old, new)
MUTANTS = [
    ("a desync child under a sync parent is not held", "effective-sync",
     "        if (w->sub_sync) return 1;\n        if (++i > PARENT_MAX) return 1;",
     "        return w->sub_sync;\n        if (++i > PARENT_MAX) return 1;"),
    ("set_position takes effect at once", "page",
     "    w->sub_px = clampi(x, -MAX_DIM, MAX_DIM);\n    w->sub_py = clampi(y, -MAX_DIM, MAX_DIM);",
     "    w->sub_px = w->sub_x = clampi(x, -MAX_DIM, MAX_DIM);\n    w->sub_py = w->sub_y = clampi(y, -MAX_DIM, MAX_DIM);"),
    ("a synchronized commit shows without its parent", "sync",
     "    if (w->role == ROLE_SUBSURFACE && eff_sync(w)) return;   /* waits for the parent */",
     "    if (0) return;"),
    ("a superseded cached frame is never released", "sync",
     "            wl_buffer_send_release(k->buffer);        /* never reached the screen */",
     "            ;"),
    ("attach after commit leaks into the cached frame", "snapshot",
     "    p->attached = 0; p->buffer = NULL; p->has_damage = 0;\n    watch_buffer(w, NULL);\n}\n\nstatic void drop_cache",
     "    watch_buffer(w, NULL);\n}\n\nstatic void drop_cache"),
    ("a parent's commit does not apply its children's caches", "nested-sync",
     "        if (ch->has_cache) apply_cached(ch, depth + 1);\n    }\n}\n\n/* Make `st`",
     "    }\n}\n\n/* Make `st`"),
    ("set_desync holds on to what it was holding", "set-desync",
     "    flush_desync(w, 0);\n}",
     "}"),
    ("the stack is drawn top to bottom", "zorder",
     "    for (struct wl_list *n = s->sub_cur.next; n != &s->sub_cur; n = n->next) {\n        if (n == &s->self_cur) {\n            int lw, lh; surf_lsize(s, &lw, &lh);",
     "    for (struct wl_list *n = s->sub_cur.prev; n != &s->sub_cur; n = n->prev) {\n        if (n == &s->self_cur) {\n            int lw, lh; surf_lsize(s, &lw, &lh);"),
    ("the parent is not in its own stack", "zorder",
     "    if (sib && sib == p)                                         at = &p->self_pend;",
     "    if (sib && sib == p)                                         at = p->sub_pend.next;"),
    ("restacking waits for nothing", "zorder",
     "    unlink_init(&w->pend_link);\n    wl_list_insert(above ? at : at->prev, &w->pend_link);\n}",
     "    unlink_init(&w->pend_link);\n    wl_list_insert(above ? at : at->prev, &w->pend_link);\n    children_apply(p, 0);\n}"),
    ("translucent pixels are opaque", "alpha",
     "    if (sa == 255) return s;\n    if (sa == 0)   return d;",
     "    if (sa) return s;\n    return d;"),
    ("a viewport destination is ignored", "viewport",
     "            blit_over(flat, clip, ox, oy, lw * k, lh * k, s->store, sr, s->opaque_fmt);",
     "            blit_over(flat, clip, ox, oy, sr.x1 - sr.x0, sr.y1 - sr.y0, s->store, sr, s->opaque_fmt);"),
    ("a viewport source rectangle is ignored", "viewport",
     "            if (s->current.vp_src_set) {\n                irect q",
     "            if (0) {\n                irect q"),
    ("a stretched root is the size of its buffer", "stretched-root",
     "    if (c->vp_dst_w > 0 && c->vp_dst_h > 0) { x = c->vp_dst_w; y = c->vp_dst_h; }\n    else if (c->vp_src_set)                 { x = c->vp_sw;    y = c->vp_sh; }",
     "    if (0) { x = c->vp_dst_w; y = c->vp_dst_h; }\n    else if (c->vp_src_set)                 { x = c->vp_sw;    y = c->vp_sh; }"),
    ("only the damaged part is repainted, one pixel too small", "damage",
     "        dirty_add(r, ox + buf->x0, oy + buf->y0, ox + buf->x1, oy + buf->y1);",
     "        dirty_add(r, ox + buf->x0 + 1, oy + buf->y0 + 1, ox + buf->x1 - 1, oy + buf->y1 - 1);"),
    ("a destroyed subsurface stays until the parent commits", "unmap",
     "        dirty_all(w);                      /* while the chain to the window is intact */\n        unlink_init(&w->pend_link);\n        unlink_init(&w->cur_link);",
     "        unlink_init(&w->pend_link);\n        unlink_init(&w->cur_link);"),
    ("an empty input region still takes the pointer", "pointer",
     "    case IN_NONE: return 0;",
     "    case IN_NONE: return 1;"),
    ("the pointer ignores subsurfaces", "pointer",
     "            hit = pick(w, sx, sy, &lx, &ly, 0);",
     "            hit = NULL;"),
    ("the pointer is told the toplevel's coordinates", "pointer-nested",
     "        aurwl_win *hit = pick(ch, x - ch->sub_x, y - ch->sub_y, lx, ly, depth + 1);",
     "        aurwl_win *hit = pick(ch, x - ch->sub_x - 1, y - ch->sub_y, lx, ly, depth + 1);"),
    ("no implicit grab", "pointer",
     "        if (c->btns_down > 0 && c->ptr_surf && c->ptr_focus == w",
     "        if (0 && c->btns_down > 0 && c->ptr_surf && c->ptr_focus == w"),
    ("the pointer is not told it left the page", "pointer",
     "static void ptr_leave(aurwl *c)\n{\n    aurwl_win *s = c->ptr_surf;\n    c->ptr_surf = NULL;\n    if (!s) return;",
     "static void ptr_leave(aurwl *c)\n{\n    aurwl_win *s = c->ptr_surf;\n    c->ptr_surf = NULL;\n    if (1) return;"),
    ("frame callbacks skip subsurfaces", "frame",
     "    wl_list_for_each(w, &c->surfaces, link) {\n        struct wl_resource *cb, *tmp;",
     "    wl_list_for_each(w, &c->surfaces, link) {\n        if (w->role == ROLE_SUBSURFACE) continue;\n        struct wl_resource *cb, *tmp;"),
    ("the window geometry no longer crops the flattened window", "geometry",
     "    if (w->has_geo) {\n        gx = w->geo_x; gy = w->geo_y; gw = w->geo_w; gh = w->geo_h;\n        /* Every one",
     "    if (0) {\n        gx = w->geo_x; gy = w->geo_y; gw = w->geo_w; gh = w->geo_h;\n        /* Every one"),
    ("a buffer destroyed in the cache still counts as an unmap", "wreck",
     "    w->cached.buffer = NULL;\n    w->cached.attached = 0;\n    w->cbuf_listening = NULL;",
     "    w->cached.buffer = NULL;\n    w->cbuf_listening = NULL;"),
    ("an idle inhibitor counts after its window is hidden", "idle",
     "        if (r && win_visible(r)) return 1;",
     "        if (r) return 1;"),
    ("an idle inhibitor on an unmapped surface still counts", "idle",
     "        if (!ih->w || !ih->w->store || !ih->w->mapped) continue;",
     "        if (!ih->w) continue;"),
    ("painting a window has no work limit", "budget",
     "    if (depth > PARENT_MAX || !s->store || *left <= 0) return;",
     "    if (depth > PARENT_MAX || !s->store) return;"),
    ("a drag quoting a stale serial runs", "dnd",
     "c->btns_down > 0 && serial == c->btn_serial &&",
     "c->btns_down > 0 &&"),
    ("the pointer keeps reaching programs during a drag", "dnd",
     "    if (c->drag.active && !c->drag.dropped) {       /* the pointer belongs to the drag */",
     "    if (0) {"),
    ("letting go does not end the drag", "dnd",
     "        if (!pressed) drag_release(c);\n        return;",
     "        return;"),
    ("leave is sent after a drop", "dnd",
     "    c->drag.target = NULL;           /* it was dropped on: leave is NOT sent after a drop */\n    drag_end(c);",
     "    drag_end(c);"),
    ("the action is never settled", "dnd",
     "    if (!c->drag.active || !c->drag.source) return;\n    data_source *ds",
     "    if (1) return;\n    data_source *ds"),
    ("Escape does not cancel a drag", "dnd",
     "    if (pressed && code == 1 && c->drag.active && !c->drag.dropped) drag_cancel(c);",
     ""),
    ("a drag nobody accepted is dropped", "dnd",
     "    int took = c->drag.target && c->drag.mime[0] && c->drag.chosen &&",
     "    int took = c->drag.target &&"),
    ("the target is told the wrong coordinates", "dnd",
     "        wl_data_device_send_motion(c->drag.dev, t, wl_fixed_from_int(lx), wl_fixed_from_int(ly));",
     "        wl_data_device_send_motion(c->drag.dev, t, wl_fixed_from_int(sx), wl_fixed_from_int(sy));"),
    ("text input is not entered on the toplevel", "textinput",
     "    if (textinput_focus) textinput_focus(nw, w->res);",
     "    if (textinput_focus) textinput_focus(nw, NULL);"),
]

def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else ""
    src = open(SRC).read()
    tmp = tempfile.mkdtemp(prefix="wltest-sub-mutants.")
    bad = ran = 0
    try:
        for what, scenario, old, new in MUTANTS:
            if only and only not in what and only not in scenario:
                continue
            ran += 1
            if src.count(old) != 1:
                print("STALE  %-62s (pattern matches %d times)" % (what, src.count(old)))
                bad += 1
                continue
            if old == new:
                print("NOOP   %-62s" % what)
                bad += 1
                continue
            path = os.path.join(tmp, "mutant.c")
            open(path, "w").write(src.replace(old, new))
            env = dict(os.environ, AURWL_C=path, OUT=os.path.join(tmp, "out"))
            r = subprocess.run(["sh", os.path.join(ROOT, "tools", "wltest-sub.sh"), scenario],
                               env=env, capture_output=True, text=True)
            out = r.stdout + r.stderr
            if "error:" in r.stderr and "wltest-sub" not in out.split("error:")[0][-40:]:
                print("BUILD  %-62s\n%s" % (what, r.stderr[-600:]))
                bad += 1
            elif r.returncode == 0:
                print("ALIVE  %-62s (scenario %s still passes)" % (what, scenario))
                bad += 1
            else:
                print("caught %-62s by %s" % (what, scenario))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%s: %d mutants, %d not caught" % ("FAIL" if bad else "PASS", ran, bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
