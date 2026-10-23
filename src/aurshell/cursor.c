/* cursor.c — see cursor.h. */
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cursor.h"

#define EDGE 0xFFFFFFu
#define FILL 0x101214u

/* A glyph is a small grid of 0 (nothing), 1 (edge: white) and 2 (fill:
 * black). Most are drawn here as a silhouette of '#' and get their white
 * outline from the code, one pixel all round, so that every glyph has the
 * same outline whatever its shape. The arrow was drawn by hand, outline
 * included, before this file existed, and is kept exactly. */
#define GMAX 20
typedef struct { int w, h, hx, hy; uint8_t px[GMAX * GMAX]; } glyph;

static glyph G[CUR_COUNT];
static int   built;

static void from_rows(glyph *g, const char *const *rows, int n, int hx, int hy, int outline)
{
    int w = (int)strlen(rows[0]);
    int m = outline ? 1 : 0;
    memset(g, 0, sizeof *g);
    g->w = w + 2 * m; g->h = n + 2 * m;
    g->hx = hx + m; g->hy = hy + m;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < w; x++) {
            char c = rows[y][x];
            uint8_t v = c == '#' ? 2 : c == 'X' ? 1 : 0;
            g->px[(y + m) * GMAX + x + m] = v;
        }
    if (!outline) return;
    uint8_t out[GMAX * GMAX];
    memcpy(out, g->px, sizeof out);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            if (g->px[y * GMAX + x]) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < g->w && yy < g->h && g->px[yy * GMAX + xx] == 2)
                        out[y * GMAX + x] = 1;
                }
        }
    memcpy(g->px, out, sizeof out);
}

static void transpose(glyph *dst, const glyph *src)
{
    memset(dst, 0, sizeof *dst);
    dst->w = src->h; dst->h = src->w; dst->hx = src->hy; dst->hy = src->hx;
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++) dst->px[x * GMAX + y] = src->px[y * GMAX + x];
}

static void mirror_x(glyph *g)
{
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w / 2; x++) {
            uint8_t t = g->px[y * GMAX + x];
            g->px[y * GMAX + x] = g->px[y * GMAX + g->w - 1 - x];
            g->px[y * GMAX + g->w - 1 - x] = t;
        }
    g->hx = g->w - 1 - g->hx;
}

static void build(void)
{
    if (built) return;
    built = 1;
    static const char *arrow[] = {
        "X.........",
        "XX........",
        "X#X.......",
        "X##X......",
        "X###X.....",
        "X####X....",
        "X#####X...",
        "X######X..",
        "X#######X.",
        "X####XXXXX",
        "X##X#X....",
        "X#X.X#X...",
        "XX..X#X...",
        "X....X#X..",
        ".....XXX..",
    };
    from_rows(&G[CUR_ARROW], arrow, 15, 0, 0, 0);

    /* The hand: one finger up, three knuckles, a thumb. Hotspot at the
     * fingertip. */
    static const char *hand[] = {
        "...##.......",
        "..####......",
        "..####......",
        "..####......",
        "..####......",
        "..####.#.#..",
        "..#########.",
        "###########.",
        "###########.",
        ".##########.",
        "..#########.",
        "..#########.",
        "...#######..",
        "...#######..",
    };
    from_rows(&G[CUR_HAND], hand, 14, 3, 0, 1);

    /* The text cursor: serifs and a stem. Hotspot in the middle. */
    static const char *ibeam[] = {
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "#####",
    };
    from_rows(&G[CUR_IBEAM], ibeam, 13, 2, 6, 1);

    static const char *cross[] = {
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        "###############",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
        ".......#.......",
    };
    from_rows(&G[CUR_CROSS], cross, 15, 7, 7, 1);

    /* An hourglass: she is to wait. */
    static const char *wait[] = {
        "#########",
        ".#######.",
        "..#####..",
        "...###...",
        "....#....",
        "....#....",
        "...###...",
        "..#####..",
        ".#######.",
        "#########",
    };
    from_rows(&G[CUR_WAIT], wait, 10, 4, 5, 1);

    /* A circle with a bar through it: not here. */
    {
        static char rows[15][16];
        static const char *r[15];
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 15; x++) {
                float dx = x - 7.f, dy = y - 7.f, d = sqrtf(dx * dx + dy * dy);
                int ring = d <= 7.2f && d >= 4.9f;
                int bar  = d <= 7.2f && fabsf(dx + dy) <= 1.3f;
                rows[y][x] = (ring || bar) ? '#' : '.';
            }
            rows[y][15] = 0;
            r[y] = rows[y];
        }
        from_rows(&G[CUR_NO], r, 15, 7, 7, 1);
    }

    /* Four arrows: move it. */
    static const char *move[] = {
        ".......#.......",
        "......###......",
        ".....#####.....",
        ".......#.......",
        "...#...#...#...",
        "..##...#...##..",
        ".###...#...###.",
        "#######.#######",
        ".###...#...###.",
        "..##...#...##..",
        "...#...#...#...",
        ".......#.......",
        ".....#####.....",
        "......###......",
        ".......#.......",
    };
    from_rows(&G[CUR_MOVE], move, 15, 7, 7, 1);

    /* Left-right, and (turned) up-down. */
    static const char *ew[] = {
        "...#.......#...",
        "..##.......##..",
        ".#############.",
        "..##.......##..",
        "...#.......#...",
    };
    from_rows(&G[CUR_EW], ew, 5, 7, 2, 1);
    transpose(&G[CUR_NS], &G[CUR_EW]);

    /* The diagonals: a thick line with a head at each end. */
    {
        static char rows[11][12];
        static const char *r[11];
        for (int y = 0; y < 11; y++) {
            for (int x = 0; x < 11; x++) {
                int line = abs(x - y) <= 1 && x + y >= 3 && x + y <= 17;
                int head1 = x + y <= 4, head2 = x + y >= 16;
                rows[y][x] = (line || head1 || head2) ? '#' : '.';
            }
            rows[y][11] = 0;
            r[y] = rows[y];
        }
        from_rows(&G[CUR_NWSE], r, 11, 5, 5, 1);
    }
    G[CUR_NESW] = G[CUR_NWSE];
    mirror_x(&G[CUR_NESW]);
}

int cursor_scale(float text_scale)
{
    float ts = (text_scale > 0.1f) ? text_scale : 1.f;
    int sc = (int)(2.f * (ts < 1.f ? 1.f : ts) + 0.5f);
    if (sc < 2) sc = 2;
    if (sc > 4) sc = 4;
    return sc;
}

/* wp_cursor_shape_device_v1.shape, version 1. */
int cursor_class_of_shape(int shape)
{
    switch (shape) {
    case 4:  case 16: case 17:                      return CUR_HAND;    /* pointer, grab, grabbing */
    case 9:  case 10:                               return CUR_IBEAM;   /* text, vertical text */
    case 7:  case 8:                                return CUR_CROSS;   /* cell, crosshair */
    case 5:  case 6:                                return CUR_WAIT;    /* progress, wait */
    case 14: case 15:                               return CUR_NO;      /* no drop, not allowed */
    case 13: case 32:                               return CUR_MOVE;    /* move, all scroll */
    case 18: case 25: case 26: case 30:             return CUR_EW;      /* e, w, ew, col resize */
    case 19: case 22: case 27: case 31:             return CUR_NS;      /* n, s, ns, row resize */
    case 20: case 24: case 28:                      return CUR_NESW;    /* ne, sw, nesw */
    case 21: case 23: case 29:                      return CUR_NWSE;    /* nw, se, nwse */
    default:                                        return CUR_ARROW;   /* default, menu, help, alias, copy, zoom */
    }
}

void cursor_glyph_size(int cls, int sc, int *w, int *h, int *hot_x, int *hot_y)
{
    build();
    if (cls < 0 || cls >= CUR_COUNT) cls = CUR_ARROW;
    if (sc < 1) sc = 1;
    if (w) *w = G[cls].w * sc;
    if (h) *h = G[cls].h * sc;
    if (hot_x) *hot_x = G[cls].hx * sc;
    if (hot_y) *hot_y = G[cls].hy * sc;
}

static void put(surface *s, int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    s->px[(size_t)y * s->stride + x] = 0xFF000000u | rgb;
}

void cursor_paint_glyph(surface *s, int cls, int x, int y, int sc)
{
    build();
    if (!s || x < 0 || y < 0) return;          /* the pointer is not on the screen */
    if (cls < 0 || cls >= CUR_COUNT) cls = CUR_ARROW;
    if (sc < 1) sc = 1;
    const glyph *g = &G[cls];
    int ox = x - g->hx * sc, oy = y - g->hy * sc;
    for (int r = 0; r < g->h; r++)
        for (int c = 0; c < g->w; c++) {
            uint8_t v = g->px[r * GMAX + c];
            if (!v) continue;
            for (int dy = 0; dy < sc; dy++)
                for (int dx = 0; dx < sc; dx++)
                    put(s, ox + c * sc + dx, oy + r * sc + dy, v == 2 ? FILL : EDGE);
        }
}

int cursor_image_scale_milli(int w, int h, int sc)
{
    int d = w > h ? w : h;
    if (d <= 0 || d > 96) return 1000;
    int m = (int)((24L * sc * 1000L) / d);
    if (m > 940 && m < 1060) return 1000;
    return m;
}

/* src over dst, premultiplied; the framebuffer stays opaque. */
static uint32_t over(uint32_t d, uint32_t s)
{
    uint32_t a = s >> 24, out = 0xFF000000u;
    if (a == 255) return s;
    if (a == 0) return d;
    for (int sh = 0; sh < 24; sh += 8) {
        uint32_t dv = (d >> sh) & 255, sv = (s >> sh) & 255;
        uint32_t v = (dv * (255 - a) + 127) / 255 + sv;
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}

static uint32_t sample(const surface *im, float fx, float fy)
{
    if (fx < 0.f) fx = 0.f;
    if (fy < 0.f) fy = 0.f;
    if (fx > (float)(im->w - 1)) fx = (float)(im->w - 1);
    if (fy > (float)(im->h - 1)) fy = (float)(im->h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < im->w ? x0 + 1 : x0, y1 = y0 + 1 < im->h ? y0 + 1 : y0;
    float tx = fx - (float)x0, ty = fy - (float)y0;
    uint32_t p[4] = { im->px[(size_t)y0 * im->stride + x0], im->px[(size_t)y0 * im->stride + x1],
                      im->px[(size_t)y1 * im->stride + x0], im->px[(size_t)y1 * im->stride + x1] };
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float a = (float)((p[0] >> sh) & 255), b = (float)((p[1] >> sh) & 255);
        float c = (float)((p[2] >> sh) & 255), d = (float)((p[3] >> sh) & 255);
        float top = a + (b - a) * tx, bot = c + (d - c) * tx;
        out |= (uint32_t)(top + (bot - top) * ty + 0.5f) << sh;
    }
    return out;
}

void cursor_paint_image(surface *s, const surface *im, int hot_x, int hot_y, int x, int y, int sc)
{
    if (!s || !im || im->w <= 0 || im->h <= 0 || x < 0 || y < 0) return;
    int m = cursor_image_scale_milli(im->w, im->h, sc);
    int dw = (int)(((long)im->w * m + 500) / 1000), dh = (int)(((long)im->h * m + 500) / 1000);
    int ox = x - (int)(((long)hot_x * m) / 1000), oy = y - (int)(((long)hot_y * m) / 1000);
    for (int j = 0; j < dh; j++) {
        int py = oy + j;
        if (py < 0 || py >= s->h) continue;
        for (int i = 0; i < dw; i++) {
            int px = ox + i;
            if (px < 0 || px >= s->w) continue;
            uint32_t sp = m == 1000 ? im->px[(size_t)j * im->stride + i]
                                    : sample(im, ((float)i + 0.5f) * 1000.f / (float)m - 0.5f,
                                                 ((float)j + 0.5f) * 1000.f / (float)m - 0.5f);
            uint32_t *d = &s->px[(size_t)py * s->stride + px];
            *d = over(*d, sp);
        }
    }
}
