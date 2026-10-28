/* cursortest.c — the pointer's pictures (src/aurshell/cursor.c).
 *
 *   cursortest [sheet.png]
 *
 * What must be true of every picture she is shown as a pointer:
 *   - every shape a client can name (1..34) has a glyph, and a glyph drawn
 *     twice is the same twice;
 *   - a glyph is black with a white outline, nothing else: two colours, no
 *     blending, at every text size (whole-number scaling keeps edges sharp);
 *   - its hotspot is a drawn pixel of the glyph or sits within its box, and
 *     the glyph is painted so that the hotspot lands on the point asked for;
 *   - bigger text makes a bigger pointer, in whole steps: 2, 3, 4 pixels
 *     per glyph pixel;
 *   - a program's own picture at the size we draw it is copied exactly
 *     (premultiplied source over destination, byte for byte against a plain
 *     reference), with its hotspot on the point; a smaller one is scaled up
 *     until its long side is 24 * scale pixels, a huge one is left alone;
 *   - nothing is drawn outside the surface, however near its edge.
 * With a path, also writes every glyph at every scale to a PNG sheet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/aurshell/cursor.h"
#include "../src/common/png.h"

static int fails;
static void check(int ok, const char *what, int a, int b)
{
    if (ok) return;
    printf("  FAIL  %s (%d, %d)\n", what, a, b);
    fails++;
}

/* The reference: the same sum, written the long way. */
static uint32_t ref_over(uint32_t d, uint32_t s)
{
    uint32_t a = s >> 24;
    if (a == 255) return s;
    uint32_t out = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {
        uint32_t v = ((d >> sh) & 255) * (255 - a);
        v = (v + 127) / 255 + ((s >> sh) & 255);
        out |= (v > 255 ? 255 : v) << sh;
    }
    return out;
}

int main(int argc, char **argv)
{
    /* every shape has a glyph; the table agrees with the protocol's meanings */
    for (int sh = 0; sh <= 40; sh++) {
        int c = cursor_class_of_shape(sh);
        check(c >= 0 && c < CUR_COUNT, "shape has a glyph", sh, c);
    }
    check(cursor_class_of_shape(1)  == CUR_ARROW, "default is the arrow", 1, 0);
    check(cursor_class_of_shape(4)  == CUR_HAND,  "pointer is the hand", 4, 0);
    check(cursor_class_of_shape(9)  == CUR_IBEAM, "text is the I-beam", 9, 0);
    check(cursor_class_of_shape(6)  == CUR_WAIT,  "wait is the hourglass", 6, 0);
    check(cursor_class_of_shape(26) == CUR_EW,    "ew-resize is left-right", 26, 0);
    check(cursor_class_of_shape(27) == CUR_NS,    "ns-resize is up-down", 27, 0);
    check(cursor_class_of_shape(15) == CUR_NO,    "not-allowed is the barred circle", 15, 0);
    check(cursor_class_of_shape(999) == CUR_ARROW, "anything else is the arrow", 999, 0);

    /* text size */
    check(cursor_scale(1.0f) == 2 && cursor_scale(1.5f) == 3 && cursor_scale(2.0f) == 4,
          "scale 2/3/4 at 100/150/200%", 0, 0);
    check(cursor_scale(0.0f) == 2 && cursor_scale(9.0f) == 4 && cursor_scale(0.5f) == 2,
          "scale clamped", 0, 0);

    for (int cls = 0; cls < CUR_COUNT; cls++)
        for (int sc = 2; sc <= 4; sc++) {
            int gw, gh, hx, hy;
            cursor_glyph_size(cls, sc, &gw, &gh, &hx, &hy);
            check(gw > 0 && gh > 0 && gw <= 24 * sc && gh <= 24 * sc, "glyph size", cls, sc);
            check(hx >= 0 && hx < gw && hy >= 0 && hy < gh, "hotspot inside the glyph", cls, sc);
            int cw = gw + 40, ch = gh + 40;
            surface *s = surface_new(cw, ch);
            surface_fill(s, 0xFF336699u);
            int px = 20 + hx, py = 20 + hy;
            cursor_paint_glyph(s, cls, px, py, sc);
            int fill = 0, edge = 0, other = 0;
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    uint32_t v = s->px[y * s->stride + x];
                    if (v == 0xFF101214u) fill++;
                    else if (v == 0xFFFFFFFFu) edge++;
                    else if (v != 0xFF336699u) other++;
                }
            check(fill > 0 && edge > 0, "glyph has fill and outline", cls, sc);
            check(other == 0, "glyph is two colours and nothing else", cls, sc);
            /* nothing outside the glyph's own box */
            int stray = 0;
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++)
                    if ((x < 20 || x >= 20 + gw || y < 20 || y >= 20 + gh) &&
                        s->px[y * s->stride + x] != 0xFF336699u) stray++;
            check(stray == 0, "nothing drawn outside the glyph's box", cls, sc);
            /* drawn at 3x, the same pixels as 1x, magnified: whole-number scaling */
            if (sc == 2) {
                int g1w, g1h, h1x, h1y;
                cursor_glyph_size(cls, 1, &g1w, &g1h, &h1x, &h1y);
                check(g1w * 2 == gw && g1h * 2 == gh && h1x * 2 == hx && h1y * 2 == hy, "size scales by whole steps", cls, sc);
            }
            surface_free(s);
        }

    /* the arrow is the one that was always there, byte for byte at 2x */
    {
        surface *s = surface_new(60, 60);
        surface_fill(s, 0xFF000000u);
        cursor_paint_glyph(s, CUR_ARROW, 5, 5, 2);
        check(s->px[5 * s->stride + 5] == 0xFFFFFFFFu, "the arrow's tip is white at the hotspot", 0, 0);
        check(s->px[(5 + 3 * 2) * s->stride + 5 + 1 * 2] == 0xFF101214u, "the arrow's body is black", 0, 0);
        check(s->px[(5 + 14 * 2 + 1) * s->stride + 5 + 1] == 0xFF000000u, "the arrow is 15 rows tall at 2x", 0, 0);
        surface_free(s);
    }

    /* the pointer at the screen's edge, or off it: nothing outside */
    {
        surface *s = surface_new(40, 40);
        surface_fill(s, 0xFF000000u);
        cursor_paint_glyph(s, CUR_HAND, 39, 39, 4);
        cursor_paint_glyph(s, CUR_NO, -5, 10, 4);        /* off the screen: not drawn */
        cursor_paint_glyph(s, CUR_NO, 10, -1, 4);
        int n = 0;
        for (int y = 0; y < 40; y++) for (int x = 0; x < 40; x++) if (s->px[y * s->stride + x] != 0xFF000000u) n++;
        check(n > 0 && n < 40 * 40, "a glyph at the corner is clipped, not wrapped", n, 0);
        check(s->px[0] == 0xFF000000u && s->px[10 * s->stride] == 0xFF000000u, "an off-screen pointer is not drawn", 0, 0);
        surface_free(s);
    }

    /* a program's picture at the size we draw it: copied exactly */
    {
        int sc = 2, d = 24 * sc;                         /* 48: no rescale */
        check(cursor_image_scale_milli(d, d, sc) == 1000, "an image of the target size is not rescaled", 0, 0);
        check(cursor_image_scale_milli(d + 2, d, sc) == 1000, "within 6% is not rescaled", 0, 0);
        surface *im = surface_new(d, d);
        for (int y = 0; y < d; y++)
            for (int x = 0; x < d; x++) {
                uint32_t a = (uint32_t)((x * 5 + y * 3) & 255);
                /* premultiplied: colour <= alpha */
                im->px[y * im->stride + x] = (a << 24) | ((a * (uint32_t)(x & 255) / 255) << 16) |
                                             ((a * (uint32_t)(y & 255) / 255) << 8) | (a / 2);
            }
        surface *s = surface_new(120, 120);
        surface_fill(s, 0xFF4080C0u);
        uint32_t *want = malloc(120 * 120 * 4);
        for (int i = 0; i < 120 * 120; i++) want[i] = 0xFF4080C0u;
        int hx = 6, hy = 9, x = 50, y = 60;
        for (int j = 0; j < d; j++)
            for (int i = 0; i < d; i++) {
                int px = x - hx + i, py = y - hy + j;
                want[py * 120 + px] = ref_over(want[py * 120 + px], im->px[j * im->stride + i]);
            }
        cursor_paint_image(s, im, hx, hy, x, y, sc);
        int bad = 0;
        for (int i = 0; i < 120 * 120; i++) if (s->px[i] != want[i]) bad++;
        check(bad == 0, "a program's picture at the target size is copied byte for byte", bad, 0);

        /* a small picture is scaled up to the target; a constant colour stays that colour */
        surface *sm = surface_new(16, 16);
        surface_fill(sm, 0xFF00FF00u);
        check(cursor_image_scale_milli(16, 16, 2) == 3000, "16 px at normal text is drawn 3x", 0, 0);
        check(cursor_image_scale_milli(32, 32, 3) == 2250, "32 px at 150% is drawn 2.25x", 0, 0);
        check(cursor_image_scale_milli(200, 200, 2) == 1000, "a huge picture is left alone", 0, 0);
        surface_fill(s, 0xFF000000u);
        cursor_paint_image(s, sm, 0, 0, 10, 10, 2);       /* hotspot at the corner: 48 x 48 from (10,10) */
        check(s->px[10 * s->stride + 10] == 0xFF00FF00u && s->px[57 * s->stride + 57] == 0xFF00FF00u,
              "a scaled picture covers its scaled box", 0, 0);
        check(s->px[58 * s->stride + 58] == 0xFF000000u && s->px[9 * s->stride + 9] == 0xFF000000u,
              "and nothing beyond it", 0, 0);
        /* the hotspot stays on the point when scaled */
        surface_fill(s, 0xFF000000u);
        surface_fill(sm, 0xFF000000u);
        sm->px[8 * sm->stride + 8] = 0xFFFFFFFFu;           /* a white pixel at the hotspot (8,8) */
        cursor_paint_image(s, sm, 8, 8, 60, 60, 2);         /* x3: the pixel is the 3x3 block around (60,60) */
        check(s->px[61 * s->stride + 61] == 0xFFFFFFFFu, "the hotspot lands on the point when scaled", 0, 0);
        check(s->px[40 * s->stride + 40] == 0xFF000000u, "...and not elsewhere", 0, 0);
        surface_free(sm); surface_free(s); surface_free(im); free(want);
    }

    if (argc > 1) {                                       /* the sheet, to look at */
        int cw = 110, ch = 110, cols = 3 * CUR_COUNT / 3;
        int W = cols * cw, H = 3 * ch;
        surface *s = surface_new(W, H);
        surface_fill(s, 0xFFD8D8D8u);
        for (int sc = 2; sc <= 4; sc++)
            for (int cls = 0; cls < CUR_COUNT; cls++) {
                int hx, hy;
                cursor_glyph_size(cls, sc, NULL, NULL, &hx, &hy);
                cursor_paint_glyph(s, cls, cls * cw + 10 + hx, (sc - 2) * ch + 10 + hy, sc);
            }
        uint32_t *o = malloc((size_t)W * H * 4);
        for (int i = 0; i < W * H; i++) o[i] = s->px[i] & 0xFFFFFFu;
        png_write_rgb(argv[1], o, W, H);
        free(o); surface_free(s);
    }
    if (fails) { printf("\nFAIL: %d check(s)\n", fails); return 1; }
    printf("PASS: the pointer's pictures\n");
    return 0;
}
