/* xclip.inc — the clipboard between X programs and Wayland programs.
 *
 * #included at the end of xwm.inc. Xwayland does not bridge selections (it
 * leaves that to the compositor's window manager), so without this a copy in
 * one kind of program cannot be pasted in the other: text, and pictures.
 *
 * TWO DIRECTIONS, one rule each.
 *
 *  Wayland -> X. A Wayland program owns the clipboard (aurwl's c->selection,
 *  a wl_data_source). We take the X CLIPBOARD selection for our own window
 *  and answer X programs' requests from it: TARGETS lists what the source
 *  offers (its mime types, and the X names for text), and a request for one
 *  of them makes the source write into a pipe we hand it; when the pipe
 *  ends the bytes become the property the program asked for and it is told.
 *
 *  X -> Wayland. An X program owns CLIPBOARD (XFixes tells us). We ask it for
 *  TARGETS, turn what it can give into mime types, and offer those to the
 *  Wayland program that has the keyboard, as a wl_data_offer whose source is
 *  not a program's but us (XSEL). When it pastes we convert the selection to
 *  the target and write what arrives to the pipe it gave us. INCR (the way X
 *  programs send what is too big for one property) is understood.
 *
 *  What is bridged: CLIPBOARD, and any data of mime type "a/b" by the name an
 *  X program uses for it (image/png, text/html, text/uri-list), plus text in
 *  all its X names (UTF8_STRING, STRING, TEXT). One transfer is at most
 *  XC_MAX bytes (16 MiB: a screenshot); bigger is refused, not truncated.
 *  NOT bridged: PRIMARY (middle-click paste), MULTIPLE, and drag and drop
 *  between the two kinds of program. */

static const char XC_UTF8[]  = "text/plain;charset=utf-8";
static const char XC_PLAIN[] = "text/plain";
#define XC_JOBS        8
static int xc_timeout_ms = 10000;               /* how long a transfer may take (a test makes it short) */

/* ── names: a mime type is an atom to X programs ──────────────────── */
static const char *xc_name_of(xwm *m, uint32_t atom)
{
    if (!atom) return NULL;
    if (atom == m->atom[XA_UTF8_STRING]) return "UTF8_STRING";
    if (atom == XP_STRING)               return "STRING";
    if (atom == m->atom[XA_TEXT])        return "TEXT";
    if (atom == m->atom[XA_TARGETS])     return "TARGETS";
    for (int i = 0; i < m->n_names && i < 48; i++)
        if (m->names[i].atom == atom && m->names[i].name[0]) return m->names[i].name;
    return NULL;
}
static uint32_t xc_atom_of(xwm *m, const char *name)
{
    if (!strcmp(name, "UTF8_STRING")) return m->atom[XA_UTF8_STRING];
    if (!strcmp(name, "STRING"))      return XP_STRING;
    if (!strcmp(name, "TEXT"))        return m->atom[XA_TEXT];
    if (!strcmp(name, "TARGETS"))     return m->atom[XA_TARGETS];
    for (int i = 0; i < m->n_names && i < 48; i++)
        if (!strcmp(m->names[i].name, name)) return m->names[i].atom;
    return 0;
}
/* A slot in the table for `name` (or for `atom`, when the name is still to be
 * learned). Full: the oldest goes (a mime type that is gone is asked for again). */
static int xc_slot(xwm *m, const char *name, uint32_t atom)
{
    for (int i = 0; name && i < m->n_names && i < 48; i++) if (!strcmp(m->names[i].name, name)) return i;
    int i = m->n_names < 48 ? m->n_names++ : m->names_next++ % 48;
    m->names[i].atom = atom;
    snprintf(m->names[i].name, sizeof m->names[i].name, "%s", name ? name : "");
    return i;
}

static data_source *xc_source(aurwl *c) { return c->selection ? wl_resource_get_user_data(c->selection) : NULL; }
static int xc_offers(const data_source *ds, const char *mime)
{
    for (int i = 0; ds && i < ds->n_mimes; i++) if (!strcmp(ds->mimes[i], mime)) return 1;
    return 0;
}

static void xc_notify(xwm *m, uint32_t requestor, uint32_t selection, uint32_t target, uint32_t prop, uint32_t time)
{
    uint8_t ev[32] = {0};
    ev[0] = 31;                                              /* SelectionNotify */
    xw32(ev + 4, time); xw32(ev + 8, requestor); xw32(ev + 12, selection); xw32(ev + 16, target); xw32(ev + 20, prop);
    x_send_event(m, requestor, 0, ev);
    xflush(m);
}

/* UTF-8 to Latin-1, in place; what does not fit is '?'. */
static size_t xc_to_latin1(uint8_t *d, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n; ) {
        uint8_t b = d[i];
        if (b < 0x80) { d[o++] = b; i++; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < n && (d[i + 1] & 0xC0) == 0x80) {
            uint32_t cp = ((uint32_t)(b & 0x1F) << 6) | (d[i + 1] & 0x3F);
            d[o++] = cp <= 0xFF ? (uint8_t)cp : '?'; i += 2;
        } else {
            int need = (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
            d[o++] = '?'; i += (size_t)need;
        }
    }
    return o;
}
/* Latin-1 to UTF-8: a new buffer. */
static uint8_t *xc_from_latin1(const uint8_t *s, size_t n, size_t *out)
{
    uint8_t *d = malloc(2 * n + 1);
    if (!d) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < 0x80) d[o++] = s[i];
        else { d[o++] = (uint8_t)(0xC0 | (s[i] >> 6)); d[o++] = (uint8_t)(0x80 | (s[i] & 0x3F)); }
    }
    *out = o;
    return d;
}

/* ══ Wayland -> X ══════════════════════════════════════════════════ */
typedef struct xfer {
    struct wl_list link;
    xwm            *m;
    int             fd;
    struct wl_event_source *src, *timer;
    uint8_t        *buf;  size_t n, cap;
    uint32_t        requestor, selection, target, prop, time, type;
    int             latin1;
} xfer;

static void xfer_free(xfer *x)
{
    if (x->src)   wl_event_source_remove(x->src);
    if (x->timer) wl_event_source_remove(x->timer);
    if (x->fd >= 0) close(x->fd);
    wl_list_remove(&x->link);
    free(x->buf);
    free(x);
}
static void xfer_done(xfer *x, int ok)
{
    xwm *m = x->m;
    if (!m->dead) {
        if (ok) {
            size_t n = x->n;
            if (x->latin1) n = xc_to_latin1(x->buf, n);
            ok = x_change_prop(m, x->requestor, x->prop, x->type, 8, x->buf, n);
        }
        xc_notify(m, x->requestor, x->selection, x->target, ok ? x->prop : 0, x->time);
    }
    xfer_free(x);
}
static int xfer_timeout(void *data) { xfer_done(data, 0); return 0; }
static int xfer_read(int fd, uint32_t mask, void *data)
{
    xfer *x = data;
    (void)mask;
    for (;;) {
        if (x->n == x->cap) {
            if (x->cap >= XC_MAX) { xfer_done(x, 0); return 0; }
            size_t nc = x->cap ? x->cap * 2 : 65536;
            if (nc > XC_MAX) nc = XC_MAX;
            uint8_t *nb = realloc(x->buf, nc);
            if (!nb) { xfer_done(x, 0); return 0; }
            x->buf = nb; x->cap = nc;
        }
        ssize_t k = read(fd, x->buf + x->n, x->cap - x->n);
        if (k > 0) { x->n += (size_t)k; continue; }
        if (k == 0) { xfer_done(x, 1); return 0; }          /* the program has written it all and closed */
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return 0;
        xfer_done(x, 0);
        return 0;
    }
}

/* The Wayland selection changed (or went): the X side follows. */
static void xc_claim(xwm *m)
{
    x_set_selection_owner(m, m->check, m->atom[XA_CLIPBOARD]);
    m->clip_wl = 1;
    xflush(m);
}
static void xc_publish(xwm *m)
{
    aurwl *c = m->c;
    if (m->dead || m->state != 2) return;
    m->clip_gen++;
    data_source *ds = xc_source(c);
    if (!ds) {
        if (m->clip_wl) { x_set_selection_owner(m, 0, m->atom[XA_CLIPBOARD]); m->clip_wl = 0; xflush(m); }
        return;
    }
    m->clip_atoms_left = 0;
    for (int i = 0; i < ds->n_mimes; i++) {                 /* X names for the mime types: ask for the ones it has not met */
        const char *n = ds->mimes[i];
        if (xc_atom_of(m, n)) continue;
        size_t len = strlen(n);
        int slot = xc_slot(m, n, 0);
        uint8_t *p = xreq(m, 16, 0, 4 + len);
        if (!p) return;
        xw16(p, (uint16_t)len); memcpy(p + 4, n, len);
        xexpect(m, XK_CLIPATOM, m->clip_gen, (uint32_t)slot);
        m->clip_atoms_left++;
    }
    if (!m->clip_atoms_left) xc_claim(m); else xflush(m);
}

/* What the source is asked for when an X program wants `target`: the mime type,
 * the type the property is given, and whether the bytes must be Latin-1. */
static const char *xc_mime_for(xwm *m, const data_source *ds, uint32_t target, uint32_t *type, int *latin1)
{
    *latin1 = 0; *type = target;
    const char *text = xc_offers(ds, XC_UTF8) ? XC_UTF8 : xc_offers(ds, XC_PLAIN) ? XC_PLAIN : NULL;
    if (target == m->atom[XA_UTF8_STRING] || target == m->atom[XA_TEXT]) { *type = m->atom[XA_UTF8_STRING]; return text; }
    if (target == XP_STRING) { *latin1 = 1; return text; }
    const char *n = xc_name_of(m, target);
    return n && xc_offers(ds, n) ? n : NULL;
}

/* An X program asks for the selection we own. */
static void xc_serve(xwm *m, uint32_t requestor, uint32_t selection, uint32_t target, uint32_t prop, uint32_t time)
{
    aurwl *c = m->c;
    data_source *ds = xc_source(c);
    if (!prop) prop = target;                              /* an old program: the property is the target's name */
    if (selection != m->atom[XA_CLIPBOARD] || !ds || !m->clip_wl) { xc_notify(m, requestor, selection, target, 0, time); return; }

    if (target == m->atom[XA_TARGETS]) {
        uint32_t list[8 + 16]; size_t n = 0;
        list[n++] = m->atom[XA_TARGETS];
        if (xc_offers(ds, XC_UTF8) || xc_offers(ds, XC_PLAIN)) {
            list[n++] = m->atom[XA_UTF8_STRING]; list[n++] = m->atom[XA_TEXT]; list[n++] = XP_STRING;
        }
        for (int i = 0; i < ds->n_mimes && n < sizeof list / sizeof list[0]; i++) {
            uint32_t a = xc_atom_of(m, ds->mimes[i]);
            if (a) list[n++] = a;
        }
        if (x_change_prop(m, requestor, prop, XP_ATOM, 32, list, n)) xc_notify(m, requestor, selection, target, prop, time);
        else xc_notify(m, requestor, selection, target, 0, time);
        return;
    }
    uint32_t type; int latin1;
    const char *mime = xc_mime_for(m, ds, target, &type, &latin1);
    int jobs = 0; xfer *q;
    wl_list_for_each(q, &m->xfers, link) jobs++;
    int p[2];
    if (!mime || jobs >= XC_JOBS || pipe2(p, O_CLOEXEC) < 0) { xc_notify(m, requestor, selection, target, 0, time); return; }
    fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    xfer *x = calloc(1, sizeof *x);
    if (!x) { close(p[0]); close(p[1]); xc_notify(m, requestor, selection, target, 0, time); return; }
    x->m = m; x->fd = p[0]; x->requestor = requestor; x->selection = selection; x->target = target;
    x->prop = prop; x->time = time; x->type = type; x->latin1 = latin1;
    wl_list_insert(&m->xfers, &x->link);
    x->src = wl_event_loop_add_fd(c->loop, p[0], WL_EVENT_READABLE, xfer_read, x);
    x->timer = wl_event_loop_add_timer(c->loop, xfer_timeout, x);
    if (x->timer) wl_event_source_timer_update(x->timer, xc_timeout_ms);
    wl_data_source_send_send(c->selection, mime, p[1]);
    close(p[1]);
}

/* ══ X -> Wayland ══════════════════════════════════════════════════ */
enum { XS_QUEUED, XS_CONVERT, XS_DATA, XS_INCR, XS_WRITE };
typedef struct xpaste {
    struct wl_list link;
    xwm            *m;
    uint32_t        id, target;
    int             fd, state, latin1, tried_string;
    uint8_t        *buf;  size_t n, cap;
    size_t          woff;
    struct wl_event_source *timer, *wsrc;
} xpaste;

static void xc_next(xwm *m);
static void xpaste_free(xpaste *p)
{
    xwm *m = p->m;
    if (p->timer) wl_event_source_remove(p->timer);
    if (p->wsrc)  wl_event_source_remove(p->wsrc);
    if (p->fd >= 0) close(p->fd);                           /* the reader sees the end: an empty paste */
    wl_list_remove(&p->link);
    free(p->buf);
    free(p);
    xc_next(m);
}
static xpaste *xpaste_by_id(xwm *m, uint32_t id)
{
    xpaste *p;
    wl_list_for_each(p, &m->pastes, link) if (p->id == id) return p;
    return NULL;
}
static xpaste *xpaste_active(xwm *m)
{
    xpaste *p;
    wl_list_for_each(p, &m->pastes, link) if (p->state == XS_CONVERT || p->state == XS_DATA || p->state == XS_INCR) return p;
    return NULL;
}
static int xpaste_write(int fd, uint32_t mask, void *data);
static void xpaste_push(xpaste *p)                           /* write what is left; on EAGAIN wait for the pipe */
{
    while (p->woff < p->n) {
        ssize_t k = write(p->fd, p->buf + p->woff, p->n - p->woff);
        if (k > 0) { p->woff += (size_t)k; continue; }
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && errno == EAGAIN) {
            if (!p->wsrc) p->wsrc = wl_event_loop_add_fd(p->m->c->loop, p->fd, WL_EVENT_WRITABLE, xpaste_write, p);
            if (p->wsrc) return;
        }
        break;                                              /* the reader went away */
    }
    xpaste_free(p);
}
static int xpaste_write(int fd, uint32_t mask, void *data) { (void)fd; (void)mask; xpaste_push(data); return 0; }
static int xpaste_timeout(void *data)
{
    xpaste *p = data;
    xpaste_free(p);
    return 0;
}

static void x_convert(xwm *m, uint32_t selection, uint32_t target, uint32_t prop)
{
    uint8_t *q = xreq(m, 24, 0, 20);
    if (!q) return;
    xw32(q, m->check); xw32(q + 4, selection); xw32(q + 8, target); xw32(q + 12, prop); xw32(q + 16, 0 /* CurrentTime */);
}
static void x_get_prop_delete(xwm *m, uint32_t prop, int kind, uint32_t id)
{
    uint8_t *q = xreq(m, 20, 1 /* delete */, 20);
    if (!q) return;
    xw32(q, m->check); xw32(q + 4, prop); xw32(q + 8, 0 /* AnyPropertyType */); xw32(q + 12, 0);
    xw32(q + 16, XC_MAX / 4);
    xexpect(m, kind, id, prop);
}

static void xpaste_begin(xpaste *p)
{
    xwm *m = p->m;
    p->state = XS_CONVERT;
    x_convert(m, m->atom[XA_CLIPBOARD], p->target, m->atom[XA_AUR_SEL]);
    xflush(m);
}
static void xc_next(xwm *m)
{
    if (m->dead || xpaste_active(m)) return;
    xpaste *p;
    wl_list_for_each(p, &m->pastes, link) if (p->state == XS_QUEUED) { xpaste_begin(p); return; }
}
static void xpaste_deliver(xpaste *p)
{
    uint8_t *conv = NULL;
    if (p->latin1) {
        size_t n2 = 0;
        conv = xc_from_latin1(p->buf, p->n, &n2);
        if (conv) { free(p->buf); p->buf = conv; p->n = n2; p->cap = n2 + 1; }
    }
    p->state = XS_WRITE; p->woff = 0;
    fcntl(p->fd, F_SETFL, fcntl(p->fd, F_GETFL) | O_NONBLOCK);
    if (p->timer) { wl_event_source_remove(p->timer); p->timer = NULL; }
    xpaste_push(p);                                          /* may free p */
}

/* The wl_data_offer for an X selection was asked to receive `mime` into `fd`. */
static void xw_paste(aurwl *c, const char *mime, int fd)
{
    xwm *m = c->x.wm;
    if (!m || m->dead || !c->xsel_active || !m->clip_x) { close(fd); return; }
    int jobs = 0; xpaste *q;
    wl_list_for_each(q, &m->pastes, link) jobs++;
    if (jobs >= XC_JOBS) { close(fd); return; }
    uint32_t target = 0; int latin1 = 0;
    if (!strncmp(mime, "text/plain", 10)) {                  /* every text type is text: UTF8_STRING, else STRING */
        target = m->atom[XA_UTF8_STRING];
    } else target = xc_atom_of(m, mime);
    if (!target) { close(fd); return; }
    xpaste *p = calloc(1, sizeof *p);
    if (!p) { close(fd); return; }
    p->m = m; p->id = ++m->next_job; p->fd = fd; p->target = target; p->latin1 = latin1;
    wl_list_insert(m->pastes.prev, &p->link);
    p->timer = wl_event_loop_add_timer(c->loop, xpaste_timeout, p);
    if (p->timer) wl_event_source_timer_update(p->timer, xc_timeout_ms);
    xc_next(m);
}

/* The owner's list of what it can give is in: offer those as mime types. */
static uint32_t xq_atoms[64];
static int      xq_n;
static void xc_add_mime(aurwl *c, const char *mime)
{
    if (c->xsel_n >= 16) return;
    for (int i = 0; i < c->xsel_n; i++) if (!strcmp(c->xsel_mimes[i], mime)) return;
    snprintf(c->xsel_mimes[c->xsel_n++], sizeof c->xsel_mimes[0], "%s", mime);
}
static void xc_targets_ready(xwm *m)
{
    aurwl *c = m->c;
    c->xsel_n = 0;
    int utf8 = 0, text = 0;
    for (int i = 0; i < xq_n; i++) {
        const char *name = xc_name_of(m, xq_atoms[i]);
        if (!name) continue;
        if (xq_atoms[i] == m->atom[XA_UTF8_STRING]) utf8 = 1;
        else if (xq_atoms[i] == XP_STRING || xq_atoms[i] == m->atom[XA_TEXT]) text = 1;
        else if (strchr(name, '/') && strcmp(name, XC_UTF8) && strcmp(name, XC_PLAIN)) xc_add_mime(c, name);
        else if (!strcmp(name, XC_UTF8)) utf8 = 1;
        else if (!strcmp(name, XC_PLAIN)) text = 1;
    }
    if (utf8) { xc_add_mime(c, XC_UTF8); xc_add_mime(c, XC_PLAIN); }
    else if (text) xc_add_mime(c, XC_PLAIN);
    if (c->xsel_n > 0) {
        c->xsel_active = 1;
        if (c->selection) { wl_data_source_send_cancelled(c->selection); c->selection = NULL; }   /* the newer copy wins */
    } else c->xsel_active = 0;
    XDBG("X owns the clipboard: %d mime types offered", c->xsel_n);
    selection_broadcast(c);
}

static void xc_owner_changed(xwm *m, uint32_t owner)
{
    aurwl *c = m->c;
    m->xq_gen++; m->clip_x = owner;
    if (owner == m->check) return;                            /* that is us, taking it for a Wayland program */
    if (!owner) {
        if (c->xsel_active) { c->xsel_active = 0; c->xsel_n = 0; selection_broadcast(c); }
        return;
    }
    x_convert(m, m->atom[XA_CLIPBOARD], m->atom[XA_TARGETS], m->atom[XA_AUR_TGT]);
    xflush(m);
}

/* ── events and replies ───────────────────────────────────────────── */
static int xc_event(xwm *m, const uint8_t *e)
{
    uint8_t t = e[0] & 0x7F;
    if (m->xfixes_event && t == m->xfixes_event) {            /* XFixesSelectionNotify */
        if (xr32(e + 12) == m->atom[XA_CLIPBOARD]) xc_owner_changed(m, xr32(e + 8));
        return 1;
    }
    switch (t) {
    case 29:                                                  /* SelectionClear: an X program took it from us */
        if (xr32(e + 12) == m->atom[XA_CLIPBOARD]) m->clip_wl = 0;
        return 1;
    case 30:                                                  /* SelectionRequest */
        xc_serve(m, xr32(e + 12), xr32(e + 16), xr32(e + 20), xr32(e + 24), xr32(e + 4));
        return 1;
    case 31: {                                                /* SelectionNotify: an answer to a ConvertSelection of ours */
        if (xr32(e + 8) != m->check) return 1;
        uint32_t target = xr32(e + 16), prop = xr32(e + 20);
        if (target == m->atom[XA_TARGETS]) {
            if (!prop) { xq_n = 0; xc_targets_ready(m); return 1; }
            x_get_prop_delete(m, m->atom[XA_AUR_TGT], XK_CLIPTGT, m->xq_gen);
            xflush(m);
            return 1;
        }
        xpaste *p = xpaste_active(m);
        if (!p || p->state != XS_CONVERT || p->target != target) return 1;
        if (!prop) {                                          /* refused: text may still come as STRING */
            if (target == m->atom[XA_UTF8_STRING] && !p->tried_string) {
                p->tried_string = 1; p->latin1 = 1; p->target = XP_STRING; xpaste_begin(p);
            } else xpaste_free(p);
            return 1;
        }
        p->state = XS_DATA;
        x_get_prop_delete(m, m->atom[XA_AUR_SEL], XK_CLIPDATA, p->id);
        xflush(m);
        return 1;
    }
    case 28: {                                                /* PropertyNotify on our window: the next INCR chunk */
        xpaste *p = xpaste_active(m);
        if (p && p->state == XS_INCR && xr32(e + 8) == m->atom[XA_AUR_SEL] && e[16] == 0) {
            x_get_prop_delete(m, m->atom[XA_AUR_SEL], XK_CLIPDATA, p->id);
            xflush(m);
        }
        return 1;
    }
    default: return 0;
    }
}

static void xc_reply(xwm *m, const xpend *e, const uint8_t *r, size_t total)
{
    switch (e->kind) {
    case XK_EXT:
        if (e->a == 1 && r[8]) {                              /* BIG-REQUESTS: switch it on */
            uint8_t *p = xreq(m, r[9], 0, 0);
            if (p) xexpect(m, XK_BIGREQ, 0, 0);
        } else if (e->a == 2 && r[8]) {                       /* XFIXES: tell us when CLIPBOARD changes hands */
            uint8_t major = r[9];
            m->xfixes_major = major; m->xfixes_event = r[10];
            uint8_t *p = xreq(m, major, 0, 8);
            if (p) { xw32(p, 5); xw32(p + 4, 0); xexpect(m, XK_XFVER, 0, 0); }
            p = xreq(m, major, 2, 12);                        /* SelectSelectionInput: owner changes, window gone, client gone */
            if (p) { xw32(p, m->root); xw32(p + 4, m->atom[XA_CLIPBOARD]); xw32(p + 8, 7); }
        }
        break;
    case XK_BIGREQ: {
        uint32_t n = xr32(r + 8);
        if (n > 4u * 1024 * 1024) n = 4u * 1024 * 1024;
        if (n > m->maxreq) { m->maxreq = n; m->bigreq = 1; }
        break;
    }
    case XK_CLIPATOM: {
        uint32_t atom = xr32(r + 8);
        if (e->b < 48) m->names[e->b].atom = atom;
        if (e->a == m->clip_gen && m->clip_atoms_left > 0 && --m->clip_atoms_left == 0 && m->c->selection) xc_claim(m);
        break;
    }
    case XK_CLIPTGT: {                                        /* the owner's list of targets */
        if (e->a != m->xq_gen) break;
        size_t units = xr32(r + 16);
        xq_n = 0; m->xq_left = 0;
        if (r[1] == 32 && 32 + 4 * units <= total) {
            for (size_t i = 0; i < units && xq_n < 64; i++) xq_atoms[xq_n++] = xr32(r + 32 + 4 * i);
        }
        for (int i = 0; i < xq_n; i++) {
            if (xc_name_of(m, xq_atoms[i]) || xq_atoms[i] < 69) continue;      /* known, or a predefined one that is not text */
            int slot = xc_slot(m, NULL, xq_atoms[i]);
            uint8_t *p = xreq(m, 17, 0, 4);                   /* GetAtomName */
            if (!p) return;
            xw32(p, xq_atoms[i]);
            xexpect(m, XK_ATOMNAME, m->xq_gen, (uint32_t)slot);
            m->xq_left++;
        }
        if (!m->xq_left) xc_targets_ready(m);
        break;
    }
    case XK_ATOMNAME: {
        size_t n = xr16(r + 8);
        if (e->b < 48 && 32 + n <= total) {
            if (n > 63) n = 63;
            memcpy(m->names[e->b].name, r + 32, n); m->names[e->b].name[n] = 0;
        }
        if (e->a == m->xq_gen && m->xq_left > 0 && --m->xq_left == 0) xc_targets_ready(m);
        break;
    }
    case XK_CLIPDATA: {                                       /* what the owner put in our property */
        xpaste *p = xpaste_by_id(m, e->a);
        if (!p || (p->state != XS_DATA && p->state != XS_INCR)) break;
        uint32_t type = xr32(r + 8), after = xr32(r + 12), units = xr32(r + 16);
        int format = r[1];
        if (type == m->atom[XA_INCR] && p->state == XS_DATA) { p->state = XS_INCR; break; }   /* chunks follow */
        size_t bytes = format == 8 ? units : 0;
        if ((format != 8 && units) || 32 + bytes > total || p->n + bytes > XC_MAX || (p->state == XS_DATA && after)) {
            xpaste_free(p);                                   /* wrong shape, or more than we carry: refused, not cut short */
            break;
        }
        if (bytes) {
            if (p->n + bytes > p->cap) {
                size_t nc = p->n + bytes + 1;
                uint8_t *nb = realloc(p->buf, nc);
                if (!nb) { xpaste_free(p); break; }
                p->buf = nb; p->cap = nc;
            }
            memcpy(p->buf + p->n, r + 32, bytes); p->n += bytes;
        }
        if (p->state == XS_DATA || units == 0) xpaste_deliver(p);   /* a plain value, or the empty chunk that ends INCR */
        break;
    }
    default: break;
    }
}

static void xc_failed(xwm *m, const xpend *e)
{
    switch (e->kind) {
    case XK_CLIPDATA: { xpaste *p = xpaste_by_id(m, e->a); if (p) xpaste_free(p); break; }
    case XK_CLIPTGT:  if (e->a == m->xq_gen) { xq_n = 0; xc_targets_ready(m); } break;
    case XK_ATOMNAME: if (e->a == m->xq_gen && m->xq_left > 0 && --m->xq_left == 0) xc_targets_ready(m); break;
    case XK_CLIPATOM: if (e->a == m->clip_gen && m->clip_atoms_left > 0 && --m->clip_atoms_left == 0 && m->c->selection) xc_claim(m); break;
    default: break;
    }
}

/* The window manager is ready: ask for what the clipboard needs, and offer what a program already copied. */
static void xc_start(xwm *m)
{
    x_select_events(m, m->check, XM_PROPERTY);              /* INCR chunks arrive as property changes on our window */
    x_query_extension(m, "BIG-REQUESTS", 1);
    x_query_extension(m, "XFIXES", 2);
    if (m->c->selection) xc_publish(m);
}

static void xc_cleanup(xwm *m)
{
    xfer *x, *xt;
    wl_list_for_each_safe(x, xt, &m->xfers, link) xfer_free(x);
    xpaste *p, *pt;
    wl_list_for_each_safe(p, pt, &m->pastes, link) xpaste_free(p);
    m->c->xsel_active = 0; m->c->xsel_n = 0;
}

/* ── what the rest of aurwl.c calls ──────────────────────────────── */
static void xw_selection_changed(aurwl *c)
{
    if (c->selection) c->xsel_active = 0;                    /* a Wayland program owns it now */
    if (c->x.wm && !c->x.wm->dead) xc_publish(c->x.wm);
}
