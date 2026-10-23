/* journey.c — her first ten minutes, every screen, as she sees them.
 *
 * The owner's order: first start -> "Does everything work?" -> the look
 * she picked -> online -> her desktop -> "Get more programs" -> VLC is
 * ready -> it opens. Rendered for one look at one size, through the
 * shipping paint code (archetype, panels, card, band) in the order the
 * main loop paints them, against a folder of .desktop files standing in
 * for the image (AUROS_APP_DIRS, as tools/storetest.c does).
 *
 * It reaches the wifi panel's private state by including net.c, the way
 * tools/netsheet.c does: no door is cut into shipping code for a picture.
 *
 *   cc -O2 -std=gnu11 -o /tmp/journey tools/journey.c \
 *      -DWELCOME_STATE='"/tmp/journey.state"' src/aurshell/welcome.c \
 *      src/aurshell/notify.c src/aurshell/foot.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c src/aurshell/apps.c \
 *      src/aurshell/layouts/[*].c src/common/theme.c src/common/wall.c \
 *      src/common/font.c src/common/png.c $(pkg-config --cflags --libs dbus-1) -lm
 *   AUROS_APP_DIRS=<dir> AURSHELL_CLOCK=1761206400 \
 *      /tmp/journey <shell.conf> <look> <w> <h> <outdir> [vlc-dir]
 *
 * Writes <outdir>/<look>-<w>x<h>-<n>-<step>.png (asks, sure, yes, desktop,
 * online, ready, opens). tools/journeytest.sh
 * builds it and checks the words each screen must (and must not) say.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/net.c"
#include "../src/aurshell/foot.h"
#include "../src/aurshell/welcome.h"
#include "../src/aurshell/notify.h"
#include "../src/common/wall.h"
#include "../src/common/png.h"

#ifndef WELCOME_STATE_FILE
#define WELCOME_STATE_FILE WELCOME_STATE
#endif

static shell_ctx c;
static const shell_layout *L;
static shell_fonts F;
static surface *S, *WALL;
static char outdir[256], tag[64];
static int step_no;

static void settle(void) { if (L->step) for (int i = 0; i < 120; i++) L->step(&c, 0.05f); }

/* One frame, in the main loop's order, logged so the test can read it. */
static void shot(const char *name)
{
    draw_track_reset();
    shell_text_log_n = 0;
    /* The question is opaque paper over the desktop: what is under it is
     * not what she reads, so it is not logged as what the screen said. */
    shell_text_log_on = !c.welcome_open;
    surface body = *S;
    body.h = c.screen_h;
    L->paint(&c, &body, &F, WALL);
    shell_text_log_on = 1;
    if (!SHELL_PANEL_OPEN(&c)) notify_paint(&c, S, &F);
    net_paint(&c, S, &F);
    welcome_paint(&c, S, &F);
    foot_paint(&c, S, &F);
    shell_text_log_on = 0;

    char p[512];
    snprintf(p, sizeof p, "%s/%s-%d-%s.png", outdir, tag, ++step_no, name);
    uint32_t *o = malloc((size_t)S->w * S->h * sizeof *o);
    if (!o) exit(1);
    for (int i = 0; i < S->w * S->h; i++) o[i] = S->px[i] & 0xFFFFFFu;
    if (png_write_rgb(p, o, S->w, S->h) < 0) { fprintf(stderr, "cannot write %s\n", p); exit(1); }
    free(o);
    /* What the screen said, one line per run of words, for the test. */
    printf("== %s\n", p);
    for (int i = 0; i < shell_text_log_n; i++)
        if (shell_text_log[i].w > 0) printf("   %s\n", shell_text_log[i].s);
}

static int app_index(const char *id)
{
    for (int i = 0; i < c.n_apps; i++) if (!strcmp(c.apps[i].id, id)) return i;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: journey <shell.conf> <look> <w> <h> <outdir> [vlc-app-dir]\n");
        return 2;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    snprintf(outdir, sizeof outdir, "%s", argv[5]);
    snprintf(tag, sizeof tag, "%s-%dx%d", argv[2], w, h);

    theme_t t = {0};
    if (theme_load(&t, argv[1]) < 0) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    memset(&c, 0, sizeof c);
    shell_theme_load(&c, &t);
    char shf[128];
    snprintf(shf, sizeof shf, "shells/%s.shell", argv[2]);
    if (shell_archetype_load(&c, shf) < 0) { fprintf(stderr, "no %s\n", shf); return 1; }
    c.allow_install = c.allow_settings = c.allow_theme_change = c.allow_network = 1;
    c.text_scale = 1.f;
    c.focus = c.hover = c.foot_hover = -1;
    c.mouse_x = c.mouse_y = -1;
    shell_seed_apps(&c);
    if (shell_scan_apps(&c) <= 0) { fprintf(stderr, "AUROS_APP_DIRS found nothing\n"); return 1; }
    L = shell_layout_by_id(c.layout_id);
    c.screen_w = w;
    c.screen_h = h - foot_height(&c);
    if (L->init) L->init(&c);
    shell_fonts_load(&F, &c);

    S = surface_new(w, h);
    WALL = surface_new(w, h);
    uint32_t *tmp = malloc((size_t)w * h * sizeof *tmp);
    if (!S || !WALL || !tmp) return 1;
    wall_render(tmp, w, h, &t);
    for (int i = 0; i < w * h; i++) WALL->px[i] = 0xFF000000u | tmp[i];
    free(tmp);
    settle();

    /* 1. The first start: AurOS asks. */
    FILE *st = fopen(WELCOME_STATE_FILE, "w");
    if (!st) { perror(WELCOME_STATE_FILE); return 1; }
    fputs("phase=asking\n", st);
    fclose(st);
    welcome_init(&c);
    shot("asks");

    /* 2. She pressed "Yes, it all works": it asks once more. */
    welcome_set_page(W_SURE_YES);
    shot("sure");

    /* 3. And "Start AurOS from now on". */
    welcome_set_page(W_CONFIRMED);
    shot("yes");

    /* 3. Her desktop, in the look she picked. */
    c.welcome_open = 0;
    settle();
    shot("desktop");

    /* 4. Online: her own wifi came across from Windows and is in use. */
    {
        static char list[] = "*:Sky-8F2A:76:WPA2\n :BT-HomeHub-2200:52:WPA2\n :Ann's iPhone:38:WPA2\n";
        static char saved[] = "Sky-8F2A:802-11-wireless\n";
        N.n_saved = net_parse_saved(saved, N.saved, NET_MAX_SAVED);
        N.n_aps = net_parse_list(list, N.aps, NET_MAX_AP,
                                 (const char (*)[NET_NAME_MAX])N.saved, N.n_saved);
        N.have_wifi = 1; N.asked_devices = 1; N.scanning = 0;
        N.page = P_LIST; N.sel = -1; N.hover_act = -1;
        c.net_open = 1;
        shot("online");
        c.net_open = 0;
    }

    /* 5. She installed VLC from "Get more programs": the table is read
     * again (as appwatch makes the shell do) and the card says so. */
    if (argc > 6) {
        char dirs[1024];
        const char *had = getenv("AUROS_APP_DIRS");
        snprintf(dirs, sizeof dirs, "%s:%s", had ? had : "", argv[6]);
        setenv("AUROS_APP_DIRS", dirs, 1);
        int added[8];
        int k = shell_rescan_apps(&c, added, 8);
        for (int i = 0; i < k && i < 3; i++) {
            char sum[96];
            snprintf(sum, sizeof sum, "%s is ready", c.apps[added[i]].name);
            notify_local_open(sum, "Press here to open it.", c.apps[added[i]].id);
        }
        notify_fit(&c);
        settle();
        shot("ready");

        /* 6. She pressed the card: VLC opens. */
        int a = k > 0 ? added[0] : app_index("org.videolan.VLC");
        notify_fini();
        /* What the main loop does with a card's program. */
        int wi = a >= 0 ? shell_launch(&c, a) : -1;
        if (wi >= 0) {
            if (L->present) L->present(&c, wi); else c.focus = wi;
            settle();
            shot("opens");
        }
    }
    return 0;
}
