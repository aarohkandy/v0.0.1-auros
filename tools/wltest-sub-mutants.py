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
CURSOR = os.path.join(ROOT, "src", "aurshell", "cursor.c")
XWM = os.path.join(ROOT, "src", "aurwl", "xwm.inc")
XCLIP = os.path.join(ROOT, "src", "aurwl", "xclip.inc")

# (what is broken, scenario that must notice, old, new)
# A scenario written "cursortest" means tools/cursortest.sh against src/aurshell/cursor.c.
# A scenario written "x:NAME" is a scenario of tools/wltest-x.sh (the X window manager);
# the target (5th field) is then usually XWM, src/aurwl/xwm.inc.
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
     "            *left -= blit_over(flat, clip, ox, oy, lw * k, lh * k, s->store, sr, s->opaque_fmt);",
     "            *left -= blit_over(flat, clip, ox, oy, sr.x1 - sr.x0, sr.y1 - sr.y0, s->store, sr, s->opaque_fmt);"),
    ("scale is not capped at four", "cursortest",
     "    if (sc > 4) sc = 4;\n    return sc;", "    return sc;", CURSOR),
    ("pointer is not the hand", "cursortest",
     "    case 4:  case 16: case 17:                      return CUR_HAND;", "    case 16: case 17:                               return CUR_HAND;", CURSOR),
    ("text is not the I-beam", "cursortest",
     "    case 9:  case 10:                               return CUR_IBEAM;", "    case 10:                                        return CUR_IBEAM;", CURSOR),
    ("resizing left-right is up-down", "cursortest",
     "    case 18: case 25: case 26: case 30:             return CUR_EW;", "    case 18: case 25: case 30:                      return CUR_EW;", CURSOR),
    ("a picture a little off size is resampled", "cursortest",
     "    if (m > 940 && m < 1060) return 1000;", "", CURSOR),
    ("pictures are scaled to the wrong canvas", "cursortest",
     "    int m = (int)((24L * sc * 1000L) / d);", "    int m = (int)((20L * sc * 1000L) / d);", CURSOR),
    ("a picture's hotspot is not put on the point", "cursortest",
     "    int ox = x - (int)(((long)hot_x * m) / 1000), oy = y - (int)(((long)hot_y * m) / 1000);",
     "    int ox = x, oy = y;", CURSOR),
    ("a picture's transparency is ignored", "cursortest",
     "    if (a == 255) return s;\n    if (a == 0) return d;", "    if (a) return s;\n    return d;", CURSOR),
    ("a glyph has no outline", "cursortest",
     "    if (!outline) return;", "    return;", CURSOR),
    ("a glyph is drawn off its hotspot", "cursortest",
     "    int ox = x - g->hx * sc, oy = y - g->hy * sc;", "    int ox = x, oy = y;", CURSOR),
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
    ("any serial may change the pointer", "cursor",
     "    return c->ptr_surf && win_client(c->ptr_surf) == cl && serial == c->ptr_enter_serial;",
     "    return c->ptr_surf && win_client(c->ptr_surf) == cl;"),
    ("any program may change the pointer", "cursor-shape",
     "    return c->ptr_surf && win_client(c->ptr_surf) == cl && serial == c->ptr_enter_serial;",
     "    return c->ptr_surf && serial == c->ptr_enter_serial;"),
    ("the picture outlives the pointer entering something else", "cursor",
     "    cursor_reset(c);              /* whatever it asked for was for that surface */",
     ""),
    ("the hotspot ignores attach offsets", "cursor",
     "        if (hot_x) *hot_x = (c->cursor.hx - (w->off_x - c->cursor.ox)) * sc;",
     "        if (hot_x) *hot_x = c->cursor.hx * sc;"),
    ("a hidden pointer is the arrow", "cursor",
     "    case AURWL_CURSOR_HIDDEN: return AURWL_CURSOR_HIDDEN;",
     "    case AURWL_CURSOR_HIDDEN: return AURWL_CURSOR_DEFAULT;"),
    ("a cursor surface with no picture hides the pointer", "cursor",
     "        if (!w || !w->store) return AURWL_CURSOR_DEFAULT;",
     "        if (!w || !w->store) return AURWL_CURSOR_HIDDEN;"),
    ("a shape that does not exist is accepted", "cursor-shape",
     "        shape > WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT) {",
     "        shape > 9999) {"),
    ("the shape is never recorded", "cursor-shape",
     "    c->cursor.shape = (int)shape;\n    c->damage_seq++;",
     "    c->damage_seq++;"),
    ("a cursor surface's pixels are not the program's", "cursor",
     "        if (image) *image = w->store;",
     "        if (image) *image = w->store; if (image && w->store) w->store->px[0] ^= 1;"),
    ("text input is not entered on the toplevel", "textinput",
     "    if (textinput_focus) textinput_focus(nw, w->res);",
     "    if (textinput_focus) textinput_focus(nw, NULL);"),
    ("a client behind by more configures than are remembered is disconnected", "slow-client",
     "    if (w->n_sent > CFG_REMEMBER) {",
     "    if (0) {"),
    ("any serial is an answer", "serial-invented",
     "    uint32_t n = w->n_sent < CFG_REMEMBER ? w->n_sent : CFG_REMEMBER;",
     "    uint32_t n = w->n_sent < CFG_REMEMBER ? w->n_sent : CFG_REMEMBER; return 1;"),
    ("one window's serial answers another's", "serial-foreign",
     "    for (uint32_t i = 0; i < n; i++) if (w->sent[i] == ser) return 1;",
     "    for (uint32_t i = 0; i < n; i++) if (w->sent[i] == ser || (int32_t)(ser - w->sent[i]) < 0) return 1;"),   # any serial up to the newest
    ("the serials of a long-answered configure are accepted again", "slow-client",
     "    w->ack_floor = serial + 1;\n", "\n"),
    ("a serial not yet sent is taken for a forgotten one", "slow-future",
     "        return (int32_t)(ser - w->ack_floor) >= 0 && (int32_t)(oldest - ser) > 0;",
     "        (void)oldest; return (int32_t)(ser - w->ack_floor) >= 0;"),
    ("a viewport destination overrides the window geometry (clicks land 20% off in GTK4)", "csd-viewport",
     "    int bw = w->cw, bh = w->ch;\n    if (w->has_geo) {\n        /* The window, not the surface:",
     "    int bw = w->cw, bh = w->ch;\n    if (0) {\n        /* The window, not the surface:"),
    # ── the X window manager (src/aurwl/xwm.inc), tools/wltest-x.sh ──
    ("the window manager never takes WM_S0 (X programs hang at a socket nobody reads)", "x:takeover",
     "    x_set_selection_owner(m, m->check, m->atom[XA_WM_S0]);\n", "\n", XWM),
    ("windows are not redirected for compositing (mapped and never drawn)", "x:takeover",
     "            if (p) { xw32(p, m->root); p[4] = 1; }", "            if (p) { xw32(p, m->root); p[4] = 0; }", XWM),
    ("the window manager does not map the window it was asked to", "x:map",
     "    if (!w->mapped) x_map(m, w->id);\n    xwm_client_list(m);", "    xwm_client_list(m);", XWM),
    ("a window is not put at 0,0", "x:map",
     "    x_configure(m, w->id, XC_X | XC_Y, 0, 0, 0, 0);\n    w->x = w->y = 0;", "    w->x = w->y = 0;", XWM),
    ("close kills where it should ask", "x:close",
     "    if (w->wm_delete) x_client_message(m, w->id, m->atom[XA_WM_PROTOCOLS], m->atom[XA_WM_DELETE_WINDOW], 0);\n    else              x_kill_client(m, w->id);",
     "    x_kill_client(m, w->id);", XWM),
    ("a program that cannot be asked to close is never killed", "x:close",
     "    else              x_kill_client(m, w->id);", "    else              ;", XWM),
    ("focus does not reach the X server", "x:focus",
     "    x_set_focus(m, w->id);\n    if (w->wm_take_focus)", "    if (w->wm_take_focus)", XWM),
    ("a program that wants to be told it has focus is not told", "x:focus",
     "    if (w->wm_take_focus) x_client_message(", "    if (0) x_client_message(", XWM),
    ("the size the shell asks for ignores the program's minimum", "x:configure",
     "    if (v < 1) v = 1;\n    if (lo > 0 && v < lo) v = lo;", "    if (v < 1) v = 1;", XWM),
    ("the size the shell asks for ignores the program's maximum", "x:configure",
     "    if (hi > 0 && v > hi) v = hi;", "", XWM),
    ("the same size is asked for again and again", "x:configure",
     "    if (nw == w->cfg_w && nh == w->cfg_h) return;          /* the same ask is not made twice */", "", XWM),
    ("a menu is not a popup of the shell", "x:menu",
     "return w->role == ROLE_X11 && w->xw && w->xw->or_ && xwin_listed(w->xw); }", "return 0; }", XWM),
    ("a window that is gone stays listed", "x:destroy",
     "        if (w) { xwin_free(w); xwm_client_list(m); }", "        if (w) { xwm_client_list(m); }", XWM),
    ("a window that withdrew is not noted", "x:unmap-remap",
     "        if (w->managed && !w->or_) { w->withdrawn = 1; xwm_set_wm_state(m, w, 0 /* Withdrawn */); }", "", XWM),
    ("a control character in a title is shown", "x:title-change",
     "        if (b < 0x20 || b == 0x7F) { dst[o++] = ' '; i++; continue; }", "", XWM),
    ("an overlong UTF-8 sequence is a title", "x:title-change",
     "(need == 2 && cp < 0x800) || ", "", XWM),
    ("a surrogate is a title", "x:title-change",
     "(cp >= 0xD800 && cp <= 0xDFFF) || ", "", XWM),
    ("a reply to the wrong request is believed", "x:wrong-sequence",
     "    if (e.seq != xr16(r + 2)) { xwm_kill(m); return; }", "", XWM),
    ("a reply of any size is read", "x:hostile",
     "            total += 4 * (size_t)xr32(e + 4);\n            if (total > XW_REPLY_MAX) { xwm_kill(m); break; }",
     "            total += 4 * (size_t)xr32(e + 4);", XWM),
    ("a reply nobody asked for is believed", "x:unsolicited",
     "    if (!m->pend_n) { xwm_kill(m); return; }", "    if (!m->pend_n) return;", XWM),
    ("full screen asked by the program is not passed on", "x:requests",
     "                win_request(w, now ? AURWL_REQ_FULLSCREEN : AURWL_REQ_UNFULLSCREEN);", "", XWM),
    ("minimise asked by the program is not passed on", "x:requests",
     "xr32(e + 12) == 3) win_request(w, AURWL_REQ_MINIMIZE);", "xr32(e + 12) == 99) win_request(w, AURWL_REQ_MINIMIZE);", XWM),
    ("a window named before its surface exists is never paired", "x:surface-late",
     "        if (w->surf_id == id && !w->win) { xwm_associate(w, id); break; }", "        ;", XWM),
    ("a window can take a surface that is already something else", "x:role-kept",
     "    if (s->role != ROLE_NONE && s->role != ROLE_X11) return;     /* a role is for life */", "", XWM),
    ("Xwayland is not reaped (the window manager outlives it)", "x:x-goes-away",
     "    if (c->x.wm) { xwm_destroy(c->x.wm); c->x.wm = NULL; }\n    if (c->x.client) { wl_client_destroy(c->x.client); c->x.client = NULL; }\n    struct timespec now;",
     "    struct timespec now;", XWM),
    ("a window with no decorations is given a title bar", "x:frameless",
     "        int csd = (xr32(data) & 2) && xr32(data + 8) == 0;", "        int csd = 0;", XWM),
    ("hints that do not mention decorations take them away", "x:frameless",
     "        int csd = (xr32(data) & 2) && xr32(data + 8) == 0;", "        int csd = xr32(data + 8) == 0;", XWM),
    # ── the clipboard (src/aurwl/xclip.inc), the same scenarios ──
    ("the window manager never claims CLIPBOARD for a Wayland program's copy", "x:clip-text-wl-to-x",
     "    m->clip_wl = 1;\n    xflush(m);", "    xflush(m);", XCLIP),
    ("TARGETS leaves out the text names", "x:clip-text-wl-to-x",
     "            list[n++] = m->atom[XA_UTF8_STRING]; list[n++] = m->atom[XA_TEXT]; list[n++] = XP_STRING;", "", XCLIP),
    ("a target the program does not offer is served", "x:clip-text-wl-to-x",
     "    return n && xc_offers(ds, n) ? n : NULL;", "    return n;", XCLIP),
    ("an X program's own question is answered with a stranger's copy (the window manager's claim taken for an X program's)", "x:clip-text-wl-to-x",
     "    if (owner == m->check) return;                            /* that is us, taking it for a Wayland program */", "", XCLIP),
    ("STRING is not turned into Latin-1", "x:clip-latin1",
     "            if (x->latin1) n = xc_to_latin1(x->buf, n);", "", XCLIP),
    ("a two-byte character past Latin-1 is passed as a wrong byte", "x:clip-latin1",
     "            d[o++] = cp <= 0xFF ? (uint8_t)cp : '?'; i += 2;", "            d[o++] = (uint8_t)cp; i += 2;", XCLIP),
    ("a paste over the limit is cut short instead of refused", "x:clip-too-big",
     "|| (p->state == XS_DATA && after)) {", ") {", XCLIP),
    ("big requests are never switched on", "x:clip-image-wl-to-x",
     "        if (n > m->maxreq) { m->maxreq = n; m->bigreq = 1; }", "", XCLIP),
    ("a program that never writes holds the X program for ever", "x:clip-wl-hangs",
     "    if (x->timer) wl_event_source_timer_update(x->timer, xc_timeout_ms);", "", XCLIP),
    ("INCR is not recognised", "x:clip-incr",
     "        if (type == m->atom[XA_INCR] && p->state == XS_DATA) { p->state = XS_INCR; break; }   /* chunks follow */", "", XCLIP),
    ("the empty chunk does not end INCR", "x:clip-incr",
     "        if (p->state == XS_DATA || units == 0) xpaste_deliver(p);", "        if (p->state == XS_DATA) xpaste_deliver(p);", XCLIP),
    ("an X program's copy is not offered", "x:clip-text-x-to-wl",
     "        c->xsel_active = 1;\n        if (c->selection)", "        c->xsel_active = 0;\n        if (c->selection)", XCLIP),
    ("an X program that quit is still offered", "x:clip-text-x-to-wl",
     "        if (c->xsel_active) { c->xsel_active = 0; c->xsel_n = 0; selection_broadcast(c); }", "", XCLIP),
    ("an image type of an X program is not offered", "x:clip-image-x-to-wl",
     "        else if (strchr(name, '/') && strcmp(name, XC_UTF8) && strcmp(name, XC_PLAIN)) xc_add_mime(c, name);", "", XCLIP),
    ("an owner that refuses TARGETS is offered anyway", "x:clip-refused",
     "            if (!prop) { xq_n = 0; xc_targets_ready(m); return 1; }", "            if (!prop) { xq_atoms[0] = m->atom[XA_UTF8_STRING]; xq_n = 1; xc_targets_ready(m); return 1; }", XCLIP),
    ("a newer X copy does not cancel the older Wayland one", "x:clip-newest-wins",
     "        if (c->selection) { wl_data_source_send_cancelled(c->selection); c->selection = NULL; }   /* the newer copy wins */", "", XCLIP),
    ("a newer Wayland copy leaves the X copy offered", "x:clip-newest-wins",
     "    if (c->selection) c->xsel_active = 0;                    /* a Wayland program owns it now */", "", XCLIP),
    # ── review W02: X stacking ──
    ("giving an X window the keyboard does not raise it", "x:restack",
     "    m->active = w;\n    x_raise(m, w->id);", "    m->active = w;", XWM),
    ("an X window the shell calls active is not raised", "x:restack",
     "    if (activated && !w->activated) { x_raise(m, w->id); xflush(m); }     /* it is the window in use: on top */\n", "", XWM),
    ("the pointer entering an X window does not raise it", "x:restack",
     "            xw_raise(c, root_of(hit));          /* an X window must be the top one before Xwayland sees the pointer in it */\n", ""),
    ("a raise lowers instead", "x:restack",
     "    xw32(p + 8, 0 /* Above */);", "    xw32(p + 8, 1 /* Below */);", XWM),
]

def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else ""
    src = open(SRC).read()
    tmp = tempfile.mkdtemp(prefix="wltest-sub-mutants.")
    bad = ran = 0
    try:
        for m in MUTANTS:
            what, scenario, old, new = m[:4]
            target = m[4] if len(m) > 4 else SRC
            tsrc = open(target).read() if target != SRC else src
            if only and only not in what and only not in scenario:
                continue
            ran += 1
            if tsrc.count(old) != 1:
                print("STALE  %-62s (pattern matches %d times)" % (what, tsrc.count(old)))
                bad += 1
                continue
            if old == new:
                print("NOOP   %-62s" % what)
                bad += 1
                continue
            path = os.path.join(tmp, "mutant.c")
            open(path, "w").write(tsrc.replace(old, new))
            if scenario.startswith("x:"):
                # a copy of the compositor with a copy of xwm.inc beside it: a quoted include looks there first
                d = os.path.join(tmp, "xtree"); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
                shutil.copy(SRC if target != SRC else path, os.path.join(d, "aurwl.c"))
                shutil.copy(XWM if target != XWM else path, os.path.join(d, "xwm.inc"))
                shutil.copy(XCLIP if target != XCLIP else path, os.path.join(d, "xclip.inc"))
                env = dict(os.environ, AURWL_C=os.path.join(d, "aurwl.c"), OUT=os.path.join(tmp, "out"))
                cmd = ["sh", os.path.join(ROOT, "tools", "wltest-x.sh"), scenario[2:]]
            elif scenario == "cursortest":
                env = dict(os.environ, CURSOR_C=path, OUT=os.path.join(tmp, "out"))
                cmd = ["sh", os.path.join(ROOT, "tools", "cursortest.sh")]
            else:
                env = dict(os.environ, AURWL_C=path, OUT=os.path.join(tmp, "out"))
                cmd = ["sh", os.path.join(ROOT, "tools", "wltest-sub.sh"), scenario]
            r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace")
            out = r.stdout + r.stderr
            if "error:" in r.stderr and "wltest-" not in out.split("error:")[0][-40:]:
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
