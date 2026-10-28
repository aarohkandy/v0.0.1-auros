/* fallbacktest.c — a file called "Отчёт.pdf" is letters, not boxes.
 *
 * The UI face (Atkinson Hyperlegible) has no Cyrillic, Greek or arrows;
 * the rasterizer used to draw a box for every codepoint a face lacks.
 * font_set_fallback_path() names a second face for exactly those.
 *
 *   cc -O2 -std=gnu11 -o /tmp/fallbacktest tools/fallbacktest.c src/common/font.c -I src/common -lm
 *   /tmp/fallbacktest [primary.otf] [fallback.ttf]     (apt: fonts-atkinson-hyperlegible fonts-dejavu-core)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "font.h"

static int fail, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-70s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static int inked(font *f, const char *t)
{
    enum { W = 200, H = 60 };
    static uint32_t px[W * H];
    memset(px, 0, sizeof px);
    font_draw(f, px, W, H, W, 4.f, 40.f, t, 0xFFFFFF, 1.f);
    int n = 0;
    for (int i = 0; i < W * H; i++) if (px[i] & 0xFFFFFF) n++;
    return n;
}

int main(int argc, char **argv)
{
    const char *a = argc > 1 ? argv[1] : "/usr/share/fonts/opentype/atkinson-hyperlegible/AtkinsonHyperlegible-Regular.otf";
    const char *d = argc > 2 ? argv[2] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    font *plain = font_load(a, 20.f), *with = font_load(a, 20.f), *dv = font_load(d, 20.f);
    if (!plain || !with || !dv) { fprintf(stderr, "need %s and %s\n", a, d); return 2; }
    font_set_fallback_path(with, d);

    const char *cyr = "\xD0\x96";            /* Ж */
    const char *miss = "\xEF\xBF\xBF";       /* U+FFFF: in no font */
    printf("\na codepoint the UI face lacks\n\n");
    float box = font_text_width(plain, miss);
    ok("without a fallback, a Cyrillic letter is the box",
       font_text_width(plain, cyr) == box);
    ok("with one, it has the fallback face's own width",
       font_text_width(with, cyr) == font_text_width(dv, cyr) &&
       font_text_width(with, cyr) != box);
    ok("it is DRAWN by the fallback (the same ink as that face alone, not the box)",
       inked(with, cyr) == inked(dv, cyr) && inked(with, cyr) != inked(plain, cyr));
    ok("a whole file name measures as the sum of its letters",
       font_text_width(with, "\xD0\x9E\xD1\x82.pdf") >
       font_text_width(with, ".pdf") + 10.f);

    printf("\n  and nothing else changes\n\n");
    const char *same[] = { "Open Your files", "Wi-Fi \xE2\x80\x94 \xE2\x80\xA6 \xC3\xA9\xC3\xB1", "08:00", NULL };
    int all = 1;
    for (int i = 0; same[i]; i++)
        if (font_text_width(with, same[i]) != font_text_width(plain, same[i]) ||
            inked(with, same[i]) != inked(plain, same[i])) all = 0;
    ok("ASCII, Latin-1, the dash and the ellipsis are drawn by the UI face as before", all);
    ok("a font with no fallback still works as it did", font_text_width(plain, "Settings") > 50.f);

    font_free(plain); font_free(with); font_free(dv);
    printf("\n");
    if (fail) { printf("%d of %d wrong.\n", fail, checked); return 1; }
    printf("%d checks: letters the UI face lacks are drawn by the fallback, the rest is untouched.\n", checked);
    return 0;
}
