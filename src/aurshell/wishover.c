/* wishover.c — see wishover.h. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "wishover.h"
#include "draw.h"
#include "foot.h"


static uint64_t real_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static uint64_t (*ov_now)(void) = real_now;
void wishover_set_clock(uint64_t (*now)(void)) { ov_now = now ? now : real_now; }

/* ── state ───────────────────────────────────────────────────────── */

#define CARDS_MAX 3
#define OTEXT 400

typedef struct {
    int  kind;                  /* WO_MESSAGE / WO_REMIND / WO_KEEP      */
    char text[OTEXT];
    char wish[64];              /* the wish's title, the device's words */
    char wid[40];
    char when[24];              /* "This was for 9:00"                  */
    int  privately, revealed;
    /* a local "Keep this?" (look / text size) rather than the engine's */
    int  keep_what;
    char keep_old[24];
    uint64_t t0;
} card;

static struct {
    int  fill;                  /* the fill is up                        */
    char emoji[32];
    char text[64];
    char wish[64], wid[40];
    uint64_t t0, until;
    card cards[CARDS_MAX];
    int  n;
    int  hover;
    int  ate_press;
} O = { .hover = -1 };

int wishover_take_release(void)
{
    int a = O.ate_press;
    O.ate_press = 0;
    return a;
}

int wishover_fill_on(void) { return O.fill; }
int wishover_cards(void)   { return O.n; }

static void copy(char *d, size_t n, const char *s)
{
    if (!n) return;
    snprintf(d, n, "%s", s ? s : "");
}

void wishover_fill(shell_ctx *c, const char *emoji, const char *text,
                   int seconds, const char *wish, const char *wid)
{
    if (seconds < 1) seconds = 1;
    if (seconds > WO_FILL_MAX_S) seconds = WO_FILL_MAX_S;  /* §2.2 cap, here too */
    copy(O.emoji, sizeof O.emoji, emoji);
    copy(O.text, sizeof O.text, text);
    if (!O.emoji[0] && !O.text[0]) return;
    copy(O.wish, sizeof O.wish, wish);
    copy(O.wid, sizeof O.wid, wid);
    O.t0 = ov_now();
    O.until = O.t0 + (uint64_t)seconds * 1000u;
    O.fill = 1;
    c->wishfill_open = 1;
}

static void drop_card(int i)
{
    if (i < 0 || i >= O.n) return;
    memmove(&O.cards[i], &O.cards[i + 1], (size_t)(O.n - i - 1) * sizeof O.cards[0]);
    O.n--;
}

static card *slot_for(const char *wid, int kind)
{
    /* One per wish: a new firing replaces the old card (§3.5). */
    for (int i = 0; i < O.n; i++)
        if ((wid && *wid && !strcmp(O.cards[i].wid, wid)) ||
            (kind == WO_KEEP && O.cards[i].keep_what && O.cards[i].kind == WO_KEEP))
            return &O.cards[i];
    if (O.n == CARDS_MAX) drop_card(0);            /* the oldest goes */
    return &O.cards[O.n++];
}

void wishover_card(shell_ctx *c, int kind, const char *text,
                   const char *wish, const char *wid,
                   const char *for_time, int privately)
{
    (void)c;
    if (!text || !*text) return;
    card *k = slot_for(wid, kind);
    memset(k, 0, sizeof *k);
    k->kind = kind;
    copy(k->text, sizeof k->text, text);
    copy(k->wish, sizeof k->wish, wish);
    copy(k->wid, sizeof k->wid, wid);
    copy(k->when, sizeof k->when, for_time);
    k->privately = privately;
    k->t0 = ov_now();
}

void wishover_keep(shell_ctx *c, int what, const char *old, const char *wish)
{
    (void)c;
    card *k = slot_for(NULL, WO_KEEP);
    memset(k, 0, sizeof *k);
    k->kind = WO_KEEP;
    k->keep_what = what;
    copy(k->keep_old, sizeof k->keep_old, old);
    copy(k->wish, sizeof k->wish, wish);
    copy(k->text, sizeof k->text,
         what == WO_KEEP_TEXT ? "The words are a new size."
                              : "This computer now works a new way.");
    k->t0 = ov_now();
}

static void keep_revert(shell_ctx *c, card *k)
{
    if (k->keep_what == WO_KEEP_TEXT) wish_apply_text_scale(c, (float)atof(k->keep_old));
    else if (k->keep_what == WO_KEEP_LOOK) wish_restore_look(c, k->keep_old);
}

void wishover_clear(shell_ctx *c)
{
    O.fill = 0;
    c->wishfill_open = 0;
    /* The local "Keep this?" is not a wish's card: it stays, because
     * dropping it would leave a change she never agreed to keep. */
    for (int i = O.n - 1; i >= 0; i--)
        if (!O.cards[i].keep_what) drop_card(i);
}

int wishover_step(shell_ctx *c)
{
    int dirty = 0;
    uint64_t now = ov_now();
    if (O.fill) {
        if (now >= O.until) { O.fill = 0; c->wishfill_open = 0; }
        dirty = 1;                    /* the pop-in, then the going */
    }
    for (int i = O.n - 1; i >= 0; i--) {
        card *k = &O.cards[i];
        if (k->keep_what) {
            /* 15 s with no answer: it goes back (§7.7). */
            if (now - k->t0 >= (uint64_t)WO_KEEP_S * 1000u) {
                keep_revert(c, k);
                drop_card(i);
            }
            /* Nothing moves on screen while it waits (DESIGN-DIRECTION
             * §3 Motion): only the moment it goes back repaints. */
        }
    }
    return dirty;
}

int wishover_wait_ms(void)
{
    if (O.fill) return 16;
    for (int i = 0; i < O.n; i++) if (O.cards[i].keep_what) return 1000;
    return -1;
}

/* ── geometry ────────────────────────────────────────────────────── */

typedef struct {
    float k;
    int btn_h, gap, group, pad;
    int lines[CARDS_MAX];          /* text lines each card has room for */
    int overflow;
    rect bar;                       /* the fill's top strip           */
    rect fill_stop;
    rect card[CARDS_MAX];
    wish_target t[24];
    int n;
} ogeom;

static void add(ogeom *g, rect r, int kind, int arg)
{
    if (g->n >= (int)(sizeof g->t / sizeof g->t[0])) return;
    g->t[g->n].r = r; g->t[g->n].kind = kind; g->t[g->n].arg = arg; g->n++;
}

/* The buttons a card has, in order. */
static int card_buttons(const card *k, int *kinds)
{
    int n = 0;
    if (k->kind == WO_REMIND && k->privately && !k->revealed) {
        kinds[n++] = WOT_REVEAL;
        return n;
    }
    if (k->keep_what)               { kinds[n++] = WOT_KEEP; kinds[n++] = WOT_UNDO; return n; }
    if (k->kind == WO_KEEP)         { kinds[n++] = WOT_KEEP; kinds[n++] = WOT_STOP; return n; }
    kinds[n++] = WOT_CLOSE;
    if (k->kind == WO_REMIND && k->wid[0]) kinds[n++] = WOT_SNOOZE;
    if (k->wid[0]) kinds[n++] = WOT_STOP;
    return n;
}

static const char *ot_label(int kind, const card *k)
{
    switch (kind) {
    case WOT_STOP:   return "Stop this wish";
    case WOT_CLOSE:  return k && k->kind == WO_REMIND ? "Close this reminder" : "Close this note";
    case WOT_SNOOZE: return "Remind me in 10 minutes";
    case WOT_KEEP:   return "Keep it";
    case WOT_UNDO:   return k && k->keep_what ? "Put it back" : "Turn this wish off";
    case WOT_REVEAL: return "Show it";
    }
    return "";
}

static int est(const char *s, int px) { return (int)(0.54f * (float)px * (float)strlen(s)); }

static void olayout_k(const shell_ctx *c, int sw, int sh, ogeom *g, float k, int tight)
{
    memset(g, 0, sizeof *g);
    g->k = k;
    g->btn_h = (int)(36.f + 20.f * k); if (g->btn_h < 48) g->btn_h = 48;
    /* Tight: the buttons are the 48 px floor, not the 56 they like to be. */
    if (tight) g->btn_h = 48;
    g->gap = (int)(12.f * k); if (g->gap < 12) g->gap = 12;
    g->group = (int)(24.f * k); if (g->group < 24) g->group = 24;
    g->pad = (int)(20.f * k);
    int px = (int)(22.f * k);

    if (O.fill) {
        /* A strip across the top that says whose wish this is, with a
         * way to stop it; the rest of the screen is the fill. */
        g->bar = (rect){ 0, 0, sw, g->btn_h + 2 * 12 };
        int w = est("Stop this wish", px) + 2 * g->pad;
        if (w > sw / 2) w = sw / 2;
        g->fill_stop = (rect){ sw - w - 24, 12, w, g->btn_h };
        if (O.wid[0]) add(g, g->fill_stop, WOT_STOP, -1);
        add(g, (rect){ 0, 0, sw, sh }, WOT_FILL, -1);
        return;
    }

    /* Cards: bottom right, stacking up; never wider than half the screen
     * nor taller than half of it (docs/AI.md §3.5), and never over the
     * band, which is not in `sh`. */
    int full_h = sh + foot_height(c);
    int margin = sw <= 1100 ? 24 : 32;
    int w = (int)(600.f * k);
    if (w > sw / 2) w = sw / 2;
    if (w < 280) w = sw - 2 * margin < 280 ? sw - 2 * margin : 280;
    int inner = w - 2 * g->pad;
    int lh = (int)(26.f * k * 1.3f);
    int lab_h = (int)(18.f * k * 1.45f) + 8;
    int y = sh - margin - (c->bar_h > 0 ? c->bar_h : 0);
    for (int i = O.n - 1; i >= 0; i--) {
        const card *kd = &O.cards[i];
        int kinds[4], nb = card_buttons(kd, kinds);
        int bw[4], rows = 1, rowx = 0;
        for (int b = 0; b < nb; b++) {
            bw[b] = est(ot_label(kinds[b], kd), px) + 2 * g->pad;
            if (bw[b] < 48) bw[b] = 48;
            if (bw[b] > inner) bw[b] = inner;
            if (rowx && rowx + g->group + bw[b] > inner) { rows++; rowx = 0; }
            rowx += (rowx ? g->group : 0) + bw[b];
        }
        int lines = 3;
        int h;
        for (;;) {
            h = g->pad + lab_h + lines * lh + g->gap + rows * g->btn_h + (rows - 1) * g->gap + g->pad;
            if (h <= full_h / 2 || lines == 1) break;
            lines--;
        }
        if (h > full_h / 2) g->overflow = 1;
        g->lines[i] = lines;
        y -= h;
        if (y < margin) { g->overflow = 1; break; }   /* a short screen holds fewer */
        rect r = { sw - margin - w, y, w, h };
        g->card[i] = r;
        int bx = r.x + g->pad;
        int by = r.y + h - g->pad - rows * g->btn_h - (rows - 1) * g->gap;
        rowx = 0;
        for (int b = 0; b < nb; b++) {
            if (rowx && rowx + g->group + bw[b] > inner) { rowx = 0; by += g->btn_h + g->gap; }
            if (rowx) rowx += g->group;
            add(g, (rect){ bx + rowx, by, bw[b], g->btn_h }, kinds[b], i);
            rowx += bw[b];
        }
        y -= g->gap;
    }
}

/* Her text size if the cards fit at it; a tenth smaller at a time if
 * not, never below the theme's own size. */
static void olayout(const shell_ctx *c, int sw, int sh, ogeom *g)
{
    float k = (c->text_scale > 0.1f) ? c->text_scale : 1.f;
    for (;;) {
        olayout_k(c, sw, sh, g, k, 0);
        if (!g->overflow || k <= 1.f) break;
        k -= 0.1f;
        if (k < 1.f) k = 1.f;
    }
    if (g->overflow) olayout_k(c, sw, sh, g, k, 1);
}

int wishover_targets(const shell_ctx *c, int sw, int sh, wish_target *out, int max)
{
    ogeom g;
    olayout(c, sw, sh, &g);
    int n = g.n < max ? g.n : max;
    memcpy(out, g.t, (size_t)n * sizeof *out);
    return n;
}

/* ── the emoji: Noto Color Emoji, read where it already is ────────── */

/* A small inflate (RFC 1951), after Mark Adler's puff. Every read is
 * checked against the end of the input and every write against the end
 * of the output: the PNGs come out of a font file, and a font file is
 * data, not code. */
typedef struct {
    const uint8_t *in; size_t inn, ip;
    uint32_t bb; int bc;
    uint8_t *out; size_t outn, op;
} inf_t;

typedef struct { short count[16]; short sym[320]; } huff_t;

static int bits(inf_t *s, int need)
{
    long v = s->bb;
    while (s->bc < need) {
        if (s->ip >= s->inn) return -1;
        v |= (long)s->in[s->ip++] << s->bc;
        s->bc += 8;
    }
    s->bb = (uint32_t)(v >> need);
    s->bc -= need;
    return (int)(v & ((1L << need) - 1));
}

static int stored(inf_t *s)
{
    s->bb = 0; s->bc = 0;
    if (s->ip + 4 > s->inn) return -1;
    unsigned len = s->in[s->ip] | (s->in[s->ip + 1] << 8);
    unsigned nlen = s->in[s->ip + 2] | (s->in[s->ip + 3] << 8);
    s->ip += 4;
    if (len != (~nlen & 0xffffu)) return -1;
    if (s->ip + len > s->inn || s->op + len > s->outn) return -1;
    memcpy(s->out + s->op, s->in + s->ip, len);
    s->ip += len; s->op += len;
    return 0;
}

static int decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        int b = bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count; first += count;
        first <<= 1; code <<= 1;
    }
    return -1;
}

static int construct(huff_t *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->sym[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(inf_t *s, const huff_t *lc, const huff_t *dc)
{
    static const short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->op >= s->outn) return -1;
            s->out[s->op++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int e = bits(s, lext[sym]); if (e < 0) return -1;
            int len = lbase[sym] + e;
            int ds = decode(s, dc);
            if (ds < 0 || ds >= 30) return -1;
            e = bits(s, dext[ds]); if (e < 0) return -1;
            size_t dist = (size_t)(dbase[ds] + e);
            if (dist > s->op || s->op + (size_t)len > s->outn) return -1;
            for (int i = 0; i < len; i++, s->op++) s->out[s->op] = s->out[s->op - dist];
        }
    }
}

static int fixed(inf_t *s)
{
    static huff_t lc, dc;
    static int built;
    if (!built) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        construct(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        construct(&dc, l, 30);
        built = 1;
    }
    return codes(s, &lc, &dc);
}

static int dynamic(inf_t *s)
{
    static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    short lengths[320];
    huff_t lc, dc;
    int nlen = bits(s, 5), ndist = bits(s, 5), ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return -1;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > 286 || ndist > 30) return -1;
    int idx;
    for (idx = 0; idx < ncode; idx++) {
        int b = bits(s, 3); if (b < 0) return -1;
        lengths[order[idx]] = (short)b;
    }
    for (; idx < 19; idx++) lengths[order[idx]] = 0;
    if (construct(&lc, lengths, 19) != 0) return -1;
    idx = 0;
    while (idx < nlen + ndist) {
        int sym = decode(s, &lc);
        if (sym < 0) return -1;
        if (sym < 16) { lengths[idx++] = (short)sym; continue; }
        int len = 0, rep;
        if (sym == 16) {
            if (idx == 0) return -1;
            len = lengths[idx - 1];
            rep = bits(s, 2); if (rep < 0) return -1; rep += 3;
        } else if (sym == 17) {
            rep = bits(s, 3); if (rep < 0) return -1; rep += 3;
        } else {
            rep = bits(s, 7); if (rep < 0) return -1; rep += 11;
        }
        if (idx + rep > nlen + ndist) return -1;
        while (rep--) lengths[idx++] = (short)len;
    }
    if (lengths[256] == 0) return -1;
    int err = construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return codes(s, &lc, &dc);
}

/* Raw DEFLATE. Returns bytes written, or -1. */
static long inflate_raw(const uint8_t *in, size_t inn, uint8_t *out, size_t outn)
{
    inf_t s = { in, inn, 0, 0, 0, out, outn, 0 };
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        int err = type == 0 ? stored(&s) : type == 1 ? fixed(&s)
                : type == 2 ? dynamic(&s) : -1;
        if (err) return -1;
    } while (!last);
    return (long)s.op;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* A PNG, to premultiplied 0xAARRGGBB. Colour types 0, 2, 3, 4 and 6 at
 * 8 bits (and palettes at 1, 2, 4), not interlaced. NULL otherwise. */
static uint32_t *png_decode(const uint8_t *p, size_t n, int *ow, int *oh)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (n < 33 || memcmp(p, sig, 8)) return NULL;
    size_t i = 8;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, interlace = 0;
    uint32_t pal[256]; int npal = 0;
    uint8_t trns[256]; int ntrns = 0;
    memset(trns, 255, sizeof trns);
    uint8_t *z = NULL; size_t zn = 0;
    while (i + 12 <= n) {
        uint32_t len = be32(p + i);
        const uint8_t *type = p + i + 4, *d = p + i + 8;
        if (len > n - i - 12) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(d); h = be32(d + 4); depth = d[8]; ctype = d[9]; interlace = d[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            npal = (int)(len / 3); if (npal > 256) npal = 256;
            for (int k = 0; k < npal; k++)
                pal[k] = (uint32_t)d[3 * k] << 16 | (uint32_t)d[3 * k + 1] << 8 | d[3 * k + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            ntrns = (int)(len > 256 ? 256 : len);
            memcpy(trns, d, (size_t)ntrns);
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *nz = realloc(z, zn + len);
            if (!nz) { free(z); return NULL; }
            z = nz; memcpy(z + zn, d, len); zn += len;
        } else if (!memcmp(type, "IEND", 4)) break;
        i += 12 + len;
    }
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!z || !ch || interlace || w == 0 || h == 0 || w > 1024 || h > 1024 ||
        (depth != 8 && !(ctype == 3 && (depth == 1 || depth == 2 || depth == 4))) ||
        zn < 6) { free(z); return NULL; }
    int bpp_bits = ch * depth;
    size_t stride = ((size_t)w * (size_t)bpp_bits + 7) / 8;
    size_t raw_n = (stride + 1) * h;
    uint8_t *raw = malloc(raw_n);
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    if (!raw || !px) { free(z); free(raw); free(px); return NULL; }
    /* zlib: two header bytes, then DEFLATE, then a checksum we do not
     * need (a damaged stream fails the decode or the length). */
    long got = inflate_raw(z + 2, zn - 2, raw, raw_n);
    free(z);
    if (got != (long)raw_n) { free(raw); free(px); return NULL; }
    int bpp = (bpp_bits + 7) / 8;
    uint8_t *prev = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1);
        int f = row[0];
        uint8_t *cur = row + 1;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int cc = (prev && x >= (size_t)bpp) ? prev[x - bpp] : 0;
            int v = cur[x];
            switch (f) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: {
                int pp = a + b - cc, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - cc);
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : cc);
                break;
            }
            default: free(raw); free(px); return NULL;
            }
            cur[x] = (uint8_t)v;
        }
        for (uint32_t x = 0; x < w; x++) {
            uint32_t r, g, bl, al = 255;
            if (ctype == 3) {
                int idx;
                if (depth == 8) idx = cur[x];
                else {
                    int per = 8 / depth;
                    int byte = cur[x / (uint32_t)per];
                    int shift = 8 - depth * (int)(x % (uint32_t)per + 1);
                    idx = (byte >> shift) & ((1 << depth) - 1);
                }
                uint32_t c = idx < npal ? pal[idx] : 0;
                r = c >> 16 & 255; g = c >> 8 & 255; bl = c & 255;
                al = idx < ntrns ? trns[idx] : 255;
            } else if (ctype == 6) {
                r = cur[4 * x]; g = cur[4 * x + 1]; bl = cur[4 * x + 2]; al = cur[4 * x + 3];
            } else if (ctype == 2) {
                r = cur[3 * x]; g = cur[3 * x + 1]; bl = cur[3 * x + 2];
            } else if (ctype == 4) {
                r = g = bl = cur[2 * x]; al = cur[2 * x + 1];
            } else {
                r = g = bl = cur[x];
            }
            px[y * w + x] = al << 24 | (r * al / 255) << 16 | (g * al / 255) << 8 | (bl * al / 255);
        }
        prev = cur;
    }
    free(raw);
    *ow = (int)w; *oh = (int)h;
    return px;
}

/* The font, mapped once. Its cmap gives the glyph; CBLC says where the
 * glyph's picture is in CBDT; the picture is a PNG. */
static struct {
    int tried;
    const uint8_t *f; size_t n;
    size_t cmap, cmap_n, cblc, cblc_n, cbdt, cbdt_n;
} EF;

static void emoji_open(void)
{
    if (EF.tried) return;
    EF.tried = 1;
    int fd = open(WISH_EMOJI_FONT, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 12 || st.st_size > 64 * 1024 * 1024) { close(fd); return; }
    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return;
    EF.f = m; EF.n = (size_t)st.st_size;
    unsigned nt = be16(EF.f + 4);
    if (12 + (size_t)nt * 16 > EF.n) { EF.f = NULL; return; }
    for (unsigned i = 0; i < nt; i++) {
        const uint8_t *r = EF.f + 12 + i * 16;
        size_t off = be32(r + 8), len = be32(r + 12);
        if (off > EF.n || len > EF.n - off) continue;
        if (!memcmp(r, "cmap", 4)) { EF.cmap = off; EF.cmap_n = len; }
        if (!memcmp(r, "CBLC", 4)) { EF.cblc = off; EF.cblc_n = len; }
        if (!memcmp(r, "CBDT", 4)) { EF.cbdt = off; EF.cbdt_n = len; }
    }
}

#define IN(off, need, tab_off, tab_n) ((size_t)(off) + (size_t)(need) <= (size_t)(tab_n))

static int glyph_of(uint32_t cp)
{
    if (!EF.cmap_n || !IN(0, 4, 0, EF.cmap_n)) return 0;
    const uint8_t *t = EF.f + EF.cmap;
    unsigned n = be16(t + 2);
    size_t best12 = 0, best4 = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!IN(4 + i * 8, 8, 0, EF.cmap_n)) break;
        const uint8_t *r = t + 4 + i * 8;
        size_t off = be32(r + 4);
        if (!IN(off, 4, 0, EF.cmap_n)) continue;
        unsigned fmt = be16(t + off);
        if (fmt == 12 && !best12) best12 = off;
        if (fmt == 4 && !best4) best4 = off;
    }
    if (best12 && IN(best12, 16, 0, EF.cmap_n)) {
        const uint8_t *s = t + best12;
        uint32_t ng = be32(s + 12);
        for (uint32_t i = 0; i < ng; i++) {
            if (!IN(best12 + 16 + (size_t)i * 12, 12, 0, EF.cmap_n)) break;
            const uint8_t *g = s + 16 + i * 12;
            uint32_t a = be32(g), b = be32(g + 4), st = be32(g + 8);
            if (cp >= a && cp <= b) return (int)(st + (cp - a));
        }
    }
    if (best4 && cp < 0x10000 && IN(best4, 14, 0, EF.cmap_n)) {
        const uint8_t *s = t + best4;
        unsigned segx2 = be16(s + 6);
        if (!IN(best4, 16 + 4 * (size_t)segx2, 0, EF.cmap_n)) return 0;
        const uint8_t *ends = s + 14, *starts = ends + segx2 + 2,
                      *deltas = starts + segx2, *ros = deltas + segx2;
        for (unsigned i = 0; i < segx2 / 2; i++) {
            unsigned e = be16(ends + 2 * i), st = be16(starts + 2 * i);
            if (cp > e || cp < st) continue;
            unsigned ro = be16(ros + 2 * i);
            if (!ro) return (int)((cp + be16(deltas + 2 * i)) & 0xFFFF);
            size_t at = (size_t)(ros + 2 * i - t) + ro + 2 * (cp - st);
            if (!IN(at, 2, 0, EF.cmap_n)) return 0;
            unsigned gl = be16(t + at);
            return gl ? (int)((gl + be16(deltas + 2 * i)) & 0xFFFF) : 0;
        }
    }
    return 0;
}

/* Where glyph g's data is in CBDT, and in which image format. */
static int bitmap_of(int g, size_t *off, size_t *len, int *fmt)
{
    if (!EF.cblc_n || !EF.cbdt_n || !IN(0, 8, 0, EF.cblc_n)) return -1;
    const uint8_t *t = EF.f + EF.cblc;
    uint32_t nsz = be32(t + 4);
    size_t best = 0; int best_ppem = -1;
    for (uint32_t i = 0; i < nsz; i++) {
        size_t r = 8 + (size_t)i * 48;
        if (!IN(r, 48, 0, EF.cblc_n)) break;
        int start = be16(t + r + 40), end = be16(t + r + 42), ppem = t[r + 44];
        if (g >= start && g <= end && ppem > best_ppem) { best = r; best_ppem = ppem; }
    }
    if (best_ppem < 0) return -1;
    size_t arr = be32(t + best), nsub = be32(t + best + 8);
    for (size_t i = 0; i < nsub; i++) {
        size_t e = arr + i * 8;
        if (!IN(e, 8, 0, EF.cblc_n)) return -1;
        int first = be16(t + e), last = be16(t + e + 2);
        if (g < first || g > last) continue;
        size_t sub = arr + be32(t + e + 4);
        if (!IN(sub, 8, 0, EF.cblc_n)) return -1;
        int ifmt = be16(t + sub);
        *fmt = be16(t + sub + 2);
        size_t img = be32(t + sub + 4);
        size_t a, b;
        int idx = g - first;
        if (ifmt == 1) {
            if (!IN(sub + 8 + (size_t)idx * 4, 8, 0, EF.cblc_n)) return -1;
            a = be32(t + sub + 8 + idx * 4); b = be32(t + sub + 12 + idx * 4);
        } else if (ifmt == 3) {
            if (!IN(sub + 8 + (size_t)idx * 2, 4, 0, EF.cblc_n)) return -1;
            a = be16(t + sub + 8 + idx * 2); b = be16(t + sub + 10 + idx * 2);
        } else if (ifmt == 2) {
            if (!IN(sub, 20, 0, EF.cblc_n)) return -1;
            size_t sz = be32(t + sub + 8);
            a = sz * (size_t)idx; b = a + sz;
        } else return -1;
        if (b < a || !IN(img + a, b - a, 0, EF.cbdt_n)) return -1;
        *off = EF.cbdt + img + a;
        *len = b - a;
        return 0;
    }
    return -1;
}

static uint32_t first_cp(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) return u[0];
    if ((u[0] & 0xE0) == 0xC0 && u[1]) return (uint32_t)(u[0] & 31) << 6 | (u[1] & 63);
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2])
        return (uint32_t)(u[0] & 15) << 12 | (uint32_t)(u[1] & 63) << 6 | (u[2] & 63);
    if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3])
        return (uint32_t)(u[0] & 7) << 18 | (uint32_t)(u[1] & 63) << 12 |
               (uint32_t)(u[2] & 63) << 6 | (u[3] & 63);
    return 0;
}

const uint32_t *wishover_emoji(const char *utf8, int *w, int *h)
{
    static char key[32];
    static uint32_t *px;
    static int pw, ph;
    if (!utf8 || !*utf8) return NULL;
    if (px && !strcmp(key, utf8)) { *w = pw; *h = ph; return px; }
    emoji_open();
    if (!EF.f) return NULL;
    int g = glyph_of(first_cp(utf8));
    size_t off, len; int fmt;
    if (g <= 0 || bitmap_of(g, &off, &len, &fmt) < 0) return NULL;
    size_t skip = fmt == 17 ? 5 : fmt == 18 ? 8 : fmt == 19 ? 0 : (size_t)-1;
    if (skip == (size_t)-1 || len < skip + 4) return NULL;
    size_t dlen = be32(EF.f + off + skip);
    if (dlen > len - skip - 4) return NULL;
    int nw, nh;
    uint32_t *np = png_decode(EF.f + off + skip + 4, dlen, &nw, &nh);
    if (!np) return NULL;
    free(px);
    px = np; pw = nw; ph = nh;
    snprintf(key, sizeof key, "%s", utf8);
    *w = pw; *h = ph;
    return px;
}

/* ── painting ────────────────────────────────────────────────────── */

/* The emoji, scaled to `size` (box-filtered down from the font's 136 px),
 * premultiplied, over the surface. */
static void blit_scaled(surface *s, const uint32_t *src, int sw_, int sh_,
                        int dx, int dy, int size, float alpha)
{
    if (size < 2) return;
    float sc = (float)(sw_ > sh_ ? sw_ : sh_) / (float)size;
    int dw = (int)((float)sw_ / sc), dh = (int)((float)sh_ / sc);
    for (int y = 0; y < dh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= s->h) continue;
        int y0 = (int)((float)y * sc), y1 = (int)((float)(y + 1) * sc);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh_) y1 = sh_;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x;
            if (tx < 0 || tx >= s->w) continue;
            int x0 = (int)((float)x * sc), x1 = (int)((float)(x + 1) * sc);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw_) x1 = sw_;
            unsigned a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t p = src[yy * sw_ + xx];
                    a += p >> 24; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; n++;
                }
            if (!n || !a) continue;
            float fa = (float)a / (float)n / 255.f * alpha;
            float fr = (float)r / (float)n * alpha, fg = (float)g / (float)n * alpha,
                  fb = (float)b / (float)n * alpha;
            uint32_t *d = &s->px[ty * s->stride + tx];
            uint32_t dv = *d;
            float ia = 1.f - fa;
            unsigned or_ = (unsigned)(fr + (float)(dv >> 16 & 255) * ia);
            unsigned og  = (unsigned)(fg + (float)(dv >> 8 & 255) * ia);
            unsigned ob  = (unsigned)(fb + (float)(dv & 255) * ia);
            if (or_ > 255) or_ = 255;
            if (og > 255) og = 255;
            if (ob > 255) ob = 255;
            *d = (dv & 0xFF000000u) | or_ << 16 | og << 8 | ob;
        }
    }
}

/* Cheap and repeatable: the same screen every time for the same wish. */
static unsigned hash(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

static void paint_btn(surface *s, const shell_ctx *c, rect r, const char *label,
                      int primary, int hot, font *f)
{
    draw_round_rect(s, r, corners_all(8.f), primary ? (hot ? c->accent_warm : c->accent)
                                                    : c->surface_c, 1.f);
    if (!primary)
        draw_round_rect_border(s, r, corners_all(8.f), 2.f, hot ? c->fg : c->overlay, 1.f);
    if (f) {
        float w = shell_text_w(f, label);
        float x = (float)r.x + ((float)r.w - w) / 2.f;
        if (x < (float)r.x + 12.f) x = (float)r.x + 12.f;
        shell_text_elided(s, f, x, shell_baseline(f, (float)r.y, (float)r.h),
                          (float)r.w - 24.f, label, primary ? c->surface_c : c->fg, 1.f);
    }
}

static void paint_fill(shell_ctx *c, surface *s, shell_fonts *f, ogeom *g)
{
    uint64_t el = ov_now() - O.t0;
    /* A soft veil of her own paper colour, so the emoji read as a layer
     * over her screen rather than a different screen. */
    draw_rect(s, (rect){ 0, 0, s->w, s->h }, c->bg, 0.55f);
    int ew = 0, eh = 0;
    const uint32_t *em = O.emoji[0] ? wishover_emoji(O.emoji, &ew, &eh) : NULL;
    if (em) {
        int base = (int)((float)(s->w < s->h ? s->w : s->h) / 6.5f);
        int step = (int)((float)base * 1.25f);
        int i = 0;
        for (int row = 0, y = g->bar.h - base / 3; y < s->h + base; row++, y += step * 86 / 100)
            for (int x = (row & 1) ? -step / 2 : 0; x < s->w + base; x += step, i++) {
                unsigned hv = hash((unsigned)i * 2654435761u);
                float var = 0.82f + (float)(hv % 37) / 100.f;
                int jx = (int)(hv >> 8) % (base / 5 + 1) - base / 10;
                int jy = (int)(hv >> 16) % (base / 5 + 1) - base / 10;
                /* They fade in over 120 ms and then hold still
                 * (DESIGN-DIRECTION §3 Motion: opacity only). */
                float fade = (float)el / 120.f;
                if (fade > 1.f) fade = 1.f;
                int sz = (int)((float)base * var);
                blit_scaled(s, em, ew, eh, x + jx, y + jy, sz, fade);
            }
    } else {
        /* Words, or an emoji this computer cannot draw: the words (or
         * the wish's own name) large in the middle, in her ink. */
        const char *t = O.text[0] ? O.text : (O.wish[0] ? O.wish : "");
        font *big = wish_type_for(c, f, g->k)->head;
        if (big && *t)
            shell_text_centred(s, big, (float)s->w / 2.f,
                               (float)s->h / 2.f + font_ascent(big) / 2.f, t, c->fg_hi, 1.f);
    }
    /* Whose wish this is, and the way to stop it. */
    draw_rect(s, g->bar, c->surface_c, 1.f);
    draw_hrule(s, 0, g->bar.y + g->bar.h - 2, s->w, 2, c->overlay, 1.f);
    const wish_type *ty = wish_type_for(c, f, g->k);
    font *lf = ty->lead;
    if (lf) {
        char line[160];
        snprintf(line, sizeof line, "Your wish \xE2\x80\x9C%s\xE2\x80\x9D. Press any key to close it.",
                 O.wish[0] ? O.wish : "Wish");
        if (shell_text_w(lf, line) > (float)(g->fill_stop.x - 48))
            snprintf(line, sizeof line, "Your wish \xE2\x80\x9C%s\xE2\x80\x9D", O.wish[0] ? O.wish : "Wish");
        shell_text_elided(s, lf, 24.f, shell_baseline(lf, (float)g->bar.y, (float)g->bar.h),
                          (float)(g->fill_stop.x - 48), line, c->fg_hi, 1.f);
    }
    if (O.wid[0]) paint_btn(s, c, g->fill_stop, "Stop this wish", 0, O.hover == 0, lf);
}

void wishover_paint(shell_ctx *c, surface *s, shell_fonts *f)
{
    ogeom g;
    if (O.fill) {
        olayout(c, s->w, s->h, &g);
        paint_fill(c, s, f, &g);
        return;
    }
    if (!O.n || SHELL_PANEL_OPEN(c)) return;
    olayout(c, s->w, c->screen_h > 0 && c->screen_h <= s->h ? c->screen_h : s->h, &g);
    const wish_type *ty = wish_type_for(c, f, g.k);
    font *lab = ty->label;
    font *body = ty->sent;
    font *btn = ty->lead;
    for (int i = 0; i < O.n; i++) {
        rect r = g.card[i];
        if (r.w <= 0 || r.h <= 0) continue;
        card *k = &O.cards[i];
        /* Level 1 (DESIGN-DIRECTION §3): a card, its rule, radius 12.
         * Not a dialog: nothing behind it is dimmed or blocked. */
        draw_round_rect(s, r, corners_all(12.f), c->surface_c, 1.f);
        draw_round_rect_border(s, r, corners_all(12.f), 2.f, c->fg, 1.f);
        int x = r.x + g.pad, y = r.y + g.pad;
        char head[120];
        if (k->keep_what)            snprintf(head, sizeof head, "Keep this?");
        else if (k->kind == WO_KEEP) snprintf(head, sizeof head, "Keep this wish?");
        else if (k->wish[0])         snprintf(head, sizeof head, "Your wish \xE2\x80\x9C%s\xE2\x80\x9D", k->wish);
        else                         snprintf(head, sizeof head, "Wishes");
        if (lab) shell_text_elided(s, lab, (float)x, (float)y + font_ascent(lab),
                                   (float)(r.w - 2 * g.pad), head, c->subtle, 1.f);
        y += (int)(18.f * g.k * 1.45f) + 8;
        char text[OTEXT + 40];
        if (k->kind == WO_REMIND && k->privately && !k->revealed)
            snprintf(text, sizeof text, "You have a reminder.");
        else if (k->kind == WO_REMIND && k->when[0])
            snprintf(text, sizeof text, "%s  (This was for %s.)", k->text, k->when);
        else if (k->kind == WO_MESSAGE && k->wish[0])
            snprintf(text, sizeof text, "Your note: %s", k->text);
        else if (k->keep_what)
            snprintf(text, sizeof text, "%s If you do nothing, it goes back by itself.", k->text);
        else snprintf(text, sizeof text, "%s", k->text);
        if (body) {
            /* Up to three lines, broken at spaces. */
            const char *p = text;
            float lh = 26.f * g.k * 1.3f;
            int maxl = g.lines[i] > 0 ? g.lines[i] : 1;
            for (int ln = 0; ln < maxl && *p; ln++) {
                char buf[OTEXT + 40];
                size_t best = 0, j = 0, L = strlen(p);
                while (j <= L) {
                    if (p[j] == ' ' || p[j] == 0) {
                        memcpy(buf, p, j); buf[j] = 0;
                        if (shell_text_w(body, buf) > (float)(r.w - 2 * g.pad) && best) break;
                        best = j;
                        if (!p[j]) break;
                    }
                    j++;
                }
                if (!best) best = L;
                memcpy(buf, p, best); buf[best] = 0;
                if (ln == maxl - 1 && p[best]) {
                    shell_text_elided(s, body, (float)x, (float)y + font_ascent(body),
                                      (float)(r.w - 2 * g.pad), p, c->fg_hi, 1.f);
                } else {
                    shell_text(s, body, (float)x, (float)y + font_ascent(body), buf, c->fg_hi, 1.f);
                }
                y += (int)lh;
                p += best;
                while (*p == ' ') p++;
            }
        }
    }
    for (int t = 0; t < g.n; t++) {
        const card *k = &O.cards[g.t[t].arg];
        paint_btn(s, c, g.t[t].r, ot_label(g.t[t].kind, k),
                  g.t[t].kind == WOT_CLOSE || g.t[t].kind == WOT_KEEP ||
                  g.t[t].kind == WOT_REVEAL, O.hover == t, btn);
    }
}

/* ── pressing ────────────────────────────────────────────────────── */

static int ohit(rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

int wishover_click(shell_ctx *c, int x, int y)
{
    ogeom g;
    if (O.fill) {
        /* Taken, whatever it landed on: the program underneath cannot
         * be seen, so it must not be pressed (§3.5). */
        olayout(c, c->screen_w, c->screen_h, &g);
        if (O.wid[0] && ohit(g.fill_stop, x, y)) wish_send_stop(O.wid);
        O.fill = 0;
        c->wishfill_open = 0;
        O.ate_press = 1;
        return 1;
    }
    if (!O.n || SHELL_PANEL_OPEN(c)) return 0;
    olayout(c, c->screen_w, c->screen_h, &g);
    for (int t = 0; t < g.n; t++) {
        if (!ohit(g.t[t].r, x, y)) continue;
        int i = g.t[t].arg;
        if (i < 0 || i >= O.n) return 1;
        card *k = &O.cards[i];
        switch (g.t[t].kind) {
        case WOT_STOP:
            if (k->kind == WO_KEEP && !k->keep_what) wish_send_keep(k->wid, 0);
            else wish_send_stop(k->wid);
            drop_card(i);
            break;
        case WOT_CLOSE:  drop_card(i); break;
        case WOT_SNOOZE: wish_send_snooze(k->wid, 10); drop_card(i); break;
        case WOT_REVEAL: k->revealed = 1; break;
        case WOT_KEEP:
            if (!k->keep_what) wish_send_keep(k->wid, 1);
            drop_card(i);
            break;
        case WOT_UNDO:
            if (k->keep_what) keep_revert(c, k);
            else wish_send_keep(k->wid, 0);
            drop_card(i);
            break;
        }
        O.ate_press = 1;
        return 1;
    }
    /* On a card but not a button: taken, nothing happens. */
    for (int i = 0; i < O.n; i++) if (ohit(g.card[i], x, y)) { O.ate_press = 1; return 1; }
    return 0;
}

int wishover_key(shell_ctx *c, int k)
{
    (void)k;
    if (!O.fill) return 0;
    /* The first key ends it and is used up. */
    O.fill = 0;
    c->wishfill_open = 0;
    return 1;
}

void wishover_motion(shell_ctx *c, int x, int y)
{
    ogeom g;
    olayout(c, c->screen_w, c->screen_h, &g);
    O.hover = -1;
    for (int t = 0; t < g.n; t++)
        if (g.t[t].kind != WOT_FILL && ohit(g.t[t].r, x, y)) { O.hover = t; break; }
}
