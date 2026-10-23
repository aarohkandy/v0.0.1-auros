/* icontest.c — can she tell every icon from every other?
 *
 * DESIGN-DIRECTION §3: no two things share a silhouette. It was not
 * true: Documents and Photos wore one mark, Printers wore Settings',
 * and Zip files a bare window. Two checks:
 *
 *   1. every icon the shell can draw, at 24, 32, 48 and 64 px, differs
 *      from every other by at least 12% of its pixels (so a pair that is
 *      "the same box with different lines" fails), and every one draws
 *      something;
 *   2. the programs build/forge ships, read the way the shell reads a
 *      machine, get ten different icons.
 *
 *   cc -O2 -std=gnu11 -o /tmp/icontest tools/icontest.c \
 *      src/aurshell/apps.c src/aurshell/appwatch.c src/aurshell/foot.c \
 *      src/aurshell/draw.c src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      src/common/png.c -I src/aurshell -I src/common -lm
 *   /tmp/icontest [sheet.png]        (from the repository root)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/aurshell/shell.h"
#include "../src/aurshell/draw.h"
#include "../src/common/png.h"

static int fail, checked;
static void ok(const char *what, int good)
{
    checked++;
    if (!good) { printf("    %-70s FAIL\n", what); fail++; }
}

static const char *NAME[ICON_N] = {
    "globe", "mail", "photos", "files", "settings", "help", "window", "plus",
    "notes", "music", "terminal", "calculator", "store", "list", "wish",
    "documents", "printer", "zip", "gear", "wifi", "sound", "bigger", "smaller",
    "power", "close", "back", "forward", "check",
};

#define MAXS 64
static unsigned char mask[ICON_N][4][MAXS * MAXS];
static const int SZ[4] = { 24, 32, 48, 64 };

static void render(shell_icon ic, int si)
{
    int n = SZ[si];
    surface *s = surface_new(n, n);
    for (int i = 0; i < n * n; i++) s->px[i] = 0xFF000000u | 0xFFFFFF;
    shell_icon_draw(s, ic, n / 2.f, n / 2.f, (float)n * 0.9f, 0x000000, 0xFFFFFF, 1.f);
    for (int i = 0; i < n * n; i++)
        mask[ic][si][i] = (s->px[i] & 0xFF) < 128;          /* ink */
    surface_free(s);
}

/* the same icons are "the same" only if they are not told apart by
 * their silhouette AND their cut-outs: the whole inked image is
 * compared, since the knock-outs are what a pictogram family differs by */
static double diff(shell_icon a, shell_icon b, int si)
{
    int n = SZ[si], d = 0, ink = 0;
    for (int i = 0; i < n * n; i++) {
        d += mask[a][si][i] != mask[b][si][i];
        ink += mask[a][si][i] | mask[b][si][i];
    }
    return ink ? (double)d / (double)ink : 0.;
}

static void entry(const char *dir, const char *id, const char *name, const char *cats)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s.desktop", dir, id);
    FILE *f = fopen(p, "w");
    if (!f) { perror(p); exit(2); }
    fprintf(f, "[Desktop Entry]\nType=Application\nName=%s\nCategories=%s\nExec=true %s\n",
            name, cats, id);
    fclose(f);
}

int main(int argc, char **argv)
{
    printf("can she tell every icon from every other?\n");
    for (int ic = 0; ic < ICON_N; ic++)
        for (int si = 0; si < 4; si++) render((shell_icon)ic, si);

    for (int ic = 0; ic < ICON_N; ic++)
        for (int si = 0; si < 4; si++) {
            int n = SZ[si], ink = 0;
            for (int i = 0; i < n * n; i++) ink += mask[ic][si][i];
            char w[120];
            snprintf(w, sizeof w, "%s draws something at %d px", NAME[ic], SZ[si]);
            ok(w, ink > n * n / 40);
        }
    /* ICON_SETTINGS and ICON_GEAR are one mark under two names; A+ and A-
     * are one letter told apart by a sign and, always, by their words
     * ("Bigger text", "Smaller text": an icon is never alone). */
    for (int a = 0; a < ICON_N; a++)
        for (int b = a + 1; b < ICON_N; b++) {
            if (a == ICON_SETTINGS && b == ICON_GEAR) continue;
            if (a == ICON_BIGGER && b == ICON_SMALLER) continue;
            for (int si = 0; si < 4; si++) {
                double d = diff((shell_icon)a, (shell_icon)b, si);
                char w[160];
                snprintf(w, sizeof w, "%s and %s differ at %d px (%.0f%%)",
                         NAME[a], NAME[b], SZ[si], d * 100.);
                ok(w, d >= 0.12);
            }
        }

    /* The mark: a square turned 45 degrees, as tall as asked, anti-aliased,
     * and drawn -- the shell had no filled polygon before. */
    {
        int n = 64; double ink = 0; int corner = 0, centre = 0, top = 0;
        surface *d = surface_new(n, n);
        for (int i = 0; i < n * n; i++) d->px[i] = 0xFF000000u | 0xFFFFFF;
        draw_diamond(d, n / 2.f, n / 2.f, 40.f, 0x000000, 1.f);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) ink += 1.0 - (double)(d->px[y * n + x] & 0xFF) / 255.0;
        corner = (d->px[(n/2 - 17) * n + (n/2 - 17)] & 0xFF) > 250;   /* outside the edge */
        centre = (d->px[(n/2) * n + n/2] & 0xFF) < 5;
        top    = (d->px[(n/2 - 19) * n + n/2] & 0xFF) < 128;           /* the tip reaches h/2 */
        ok("the mark has area h*h/2 (a diamond 40 tall = 800 px)", ink > 760. && ink < 840.);
        ok("the mark is solid in the middle and empty in the corners", centre && corner);
        ok("the mark is as tall as asked", top);
        surface_free(d);
    }

    /* What the image ships. */
    char dir[] = "/tmp/icontest-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    entry(dir, "firefox", "Internet", "Network;WebBrowser;");
    entry(dir, "thunar", "Your files", "System;FileTools;FileManager;");
    entry(dir, "org.gnome.Evince", "Documents", "GNOME;GTK;Office;Viewer;Graphics;2DGraphics;VectorGraphics;");
    entry(dir, "org.xfce.mousepad", "Notes", "Utility;TextEditor;GTK;");
    entry(dir, "org.xfce.ristretto", "Photos", "GTK;Graphics;2DGraphics;RasterGraphics;Viewer;");
    entry(dir, "mpv", "Music and video", "AudioVideo;Audio;Video;Player;TV;");
    entry(dir, "mate-calc", "Calculator", "GTK;Utility;Calculator;");
    entry(dir, "system-config-printer", "Printers", "System;Settings;HardwareSettings;Printing;GTK;");
    entry(dir, "xarchiver", "Zip files", "GTK;Archiving;Utility;Compression;");
    entry(dir, "org.gnome.Software", "Get more programs", "GNOME;GTK;System;PackageManager;");
    setenv("AUROS_APP_DIRS", dir, 1);

    shell_ctx c;
    memset(&c, 0, sizeof c);
    theme_t t = {0};
    shell_theme_load(&c, &t);
    c.allow_install = c.allow_settings = 1;
    int n = shell_scan_apps(&c);
    ok("the scan found the programs the image ships", n >= 10);
    int seen[ICON_N] = {0}, dup = 0;
    for (int i = 0; i < c.n_apps; i++) {
        if (c.apps[i].role == APP_ROLE_SETTINGS) continue;   /* the shell's own */
        shell_icon k = c.apps[i].icon;
        if (seen[k]) {
            printf("    %s shares its icon (%s) with another program\n",
                   c.apps[i].name, NAME[k]);
            dup++;
        }
        seen[k] = 1;
    }
    ok("every program the image ships has an icon of its own", dup == 0);
    {
        char cmd[700];
        snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    }

    if (argc > 1) {                       /* the sheet, to LOOK at */
        int cols = 7, rows = (ICON_N + cols - 1) / cols, cell = 112;
        surface *sh = surface_new(cols * cell, rows * cell);
        for (int i = 0; i < sh->w * sh->h; i++) sh->px[i] = 0xFF000000u | 0xF6F2E9;
        for (int ic = 0; ic < ICON_N; ic++) {
            int x = (ic % cols) * cell, y = (ic / cols) * cell;
            shell_icon_draw(sh, (shell_icon)ic, x + 34.f, y + 34.f, 48.f, 0x17181A, 0xF6F2E9, 1.f);
            shell_icon_draw(sh, (shell_icon)ic, x + 86.f, y + 22.f, 24.f, 0x17181A, 0xF6F2E9, 1.f);
            shell_icon_draw(sh, (shell_icon)ic, x + 86.f, y + 48.f, 17.f, 0x17181A, 0xF6F2E9, 1.f);
            shell_icon_draw(sh, (shell_icon)ic, x + 56.f, y + 88.f, 32.f, 0x0B5D4F, 0xF6F2E9, 1.f);
        }
        uint32_t *o = malloc((size_t)sh->w * sh->h * sizeof *o);
        for (int i = 0; i < sh->w * sh->h; i++) o[i] = sh->px[i] & 0xFFFFFFu;
        png_write_rgb(argv[1], o, sh->w, sh->h);
        free(o);
    }

    printf("\n%d checks, %d failed\n", checked, fail);
    return fail ? 1 : 0;
}
