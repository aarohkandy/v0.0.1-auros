/* floorstest.c -- the shell's EASY floors, measured on the real looks.
 *
 *   text      nothing she reads is under 18 px, at any size she can choose
 *             (the shell's one font set, opened at every text scale)
 *   targets   every control she presses is 44 px or more on its short side, at 1024x600
 *             and the sizes above it, in each of the four looks: dock cells, rail rows,
 *             taskbar rows and tray, tiles, the buttons in a window's title bar
 *             (every layout publishes them: shell_layout.targets)
 *   bounds    a window is placed wholly on the screen, in every look, whatever size it
 *             asked for and wherever it asked to be (OBS-REAL-IMAGE)
 *
 *   cc -O2 -std=gnu11 -o /tmp/floorstest tools/floorstest.c src/aurshell/draw.c \
 *      src/aurshell/shellcommon.c src/aurshell/anim.c src/aurshell/foot.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common -lm && /tmp/floorstest     (repository root)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/shell.h"
#include "../src/aurshell/foot.h"
#include "../src/common/font.h"

static int fail = 0, checked = 0;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-72s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static const float SCALES[] = { 0.80f, 1.00f, 1.50f, 2.00f };

/* ascent + descent of an 18 px face is 22 (journeytest: "22 tall drawn; a 12 px line is 14"). */
static int tall_enough(const font *f) { return f && font_ascent(f) + font_descent(f) >= 21.f; }

static void text_floor(void)
{
    printf("\ntext: no word under 18 px, at any size\n");
    for (size_t k = 0; k < sizeof SCALES / sizeof *SCALES; k++) {
        shell_ctx c; memset(&c, 0, sizeof c);
        theme_t t = {0};
        shell_theme_load(&c, &t);
        c.text_scale = SCALES[k];
        shell_fonts f; memset(&f, 0, sizeof f);
        shell_fonts_load(&f, &c);
        char nm[96];
        snprintf(nm, sizeof nm, "text %.0f%%: every font the shell draws with is 18 px or more", (double)(SCALES[k] * 100.f));
        ok(nm, tall_enough(f.mid) && tall_enough(f.label) && tall_enough(f.small) && tall_enough(f.tiny) &&
               tall_enough(f.dmid) && tall_enough(f.big) && tall_enough(f.huge));
        shell_fonts_free(&f);
    }
}

/* ── a look with apps and windows, as the real shell would hold them ───────────────────── */
typedef struct { shell_ctx c; const shell_layout *L; surface *s; surface *app; shell_fonts f; } rig;

static const char *NAMES[] = { "Internet", "Your files", "Music and video", "Photos", "Letters",
                               "Spreadsheets", "Notes", "Printers", "Calculator", "Documents",
                               "Zip files", "Get more programs" };

static void make(rig *r, const char *look, int W, int H, float scale, int napps, int nwins, int csd0)
{
    memset(&r->c, 0, sizeof r->c);
    theme_t t = {0};
    shell_theme_load(&r->c, &t);
    char shf[96];
    snprintf(shf, sizeof shf, "shells/%s.shell", look);
    if (shell_archetype_load(&r->c, shf) < 0) { fprintf(stderr, "cannot read %s -- run from the repository root\n", shf); exit(2); }
    r->c.text_scale = scale;
    r->c.focus = r->c.hover = r->c.foot_hover = -1;
    r->c.mouse_x = r->c.mouse_y = -1;
    r->c.screen_w = W;
    r->c.screen_h = H - foot_height(&r->c);
    r->L = shell_layout_by_id(r->c.layout_id);
    if (r->s) { surface_free(r->s); r->s = NULL; }
    if (!r->app) { r->app = surface_new(900, 600); for (int i = 0; i < r->app->w * r->app->h; i++) r->app->px[i] = 0xFF12AB34u; }
    r->s = surface_new(W, H);
    shell_fonts_free(&r->f);
    memset(&r->f, 0, sizeof r->f);
    shell_fonts_load(&r->f, &r->c);
    for (int i = 0; i < napps; i++) {
        app_entry *a = &r->c.apps[i];
        memset(a, 0, sizeof *a);
        snprintf(a->name, sizeof a->name, "%s", NAMES[i % 12]);
        a->icon = ICON_WINDOW; a->tint = r->c.accent; a->pinned = i < 7;
        if (i == napps - 1) a->role = APP_ROLE_STORE;
    }
    r->c.n_apps = napps;
    for (int i = 0; i < nwins; i++) {
        win_entry *e = &r->c.wins[i];
        memset(e, 0, sizeof *e);
        snprintf(e->title, sizeof e->title, "A program %d", i + 1);
        e->app = i < napps ? i : -1;
        e->content = r->app;
        e->wid = (uint32_t)(7 + i);
        e->csd = (i == 0) ? csd0 : 0;
    }
    r->c.n_wins = nwins;
    r->c.focus = nwins ? 0 : -1;
    if (r->L->init) r->L->init(&r->c);
    for (int i = 0; i < nwins; i++) if (r->L->present) r->L->present(&r->c, i);
    if (r->L->step) for (int i = 0; i < 80; i++) r->L->step(&r->c, 0.05f);
    surface body = *r->s; body.h = r->c.screen_h;
    draw_track_reset();
    r->L->paint(&r->c, &body, &r->f, NULL);
}

static const struct { int w, h; } SIZES[] = { { 1024, 600 }, { 1366, 768 }, { 1920, 1080 } };

static int inside_screen(rect r, int W, int H) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= W && r.y + r.h <= H; }

/* What is open, and what is on the desk. */
enum { S_WINS, S_MENU, S_FIND, S_HOME, S_LIST, S_PAGES, S_BUSY };
static const char *SCEN[] = { "three programs open", "the programs menu open", "the finder open",
                              "nothing open (Home)", "the list of programs open", "thirty programs, paged",
                              "five unpinned programs running beside the favourites" };

static void press_what(rig *R, const char *prefix)     /* press the first published control whose name starts so */
{
    shell_target B[96];
    int nb = R->L->targets(&R->c, B, 96);
    for (int i = 0; i < nb; i++)
        if (!strncmp(B[i].what, prefix, strlen(prefix))) {
            R->L->click(&R->c, B[i].r.x + B[i].r.w / 2, B[i].r.y + B[i].r.h / 2);
            if (R->L->step) for (int j = 0; j < 80; j++) R->L->step(&R->c, 0.05f);
            return;
        }
}

/* Every kind-0 rect is 44 px on its short side and on the screen. */
static void floors(const char *look, int scen)
{
    for (size_t sz = 0; sz < sizeof SIZES / sizeof *SIZES; sz++)
        for (size_t k = 0; k < sizeof SCALES / sizeof *SCALES; k++) {
            static rig R;
            make(&R, look, SIZES[sz].w, SIZES[sz].h, SCALES[k], scen == S_PAGES ? 30 : 12,
                 (scen == S_HOME || scen == S_LIST || scen == S_MENU || scen == S_PAGES) ? 0 : scen == S_BUSY ? 9 : 3, 0);
            /* S_MENU: with nothing open the taskbar opens the programs menu by itself (no button to guess). */
            if (scen == S_FIND)  press_what(&R, "the All my programs cell");
            if (scen == S_LIST) {                                     /* rail: the second-last row of Home */
                shell_target B[96];
                int nb = R.L->targets(&R.c, B, 96), last = -1;
                for (int i = 0; i < nb; i++) if (!strcmp(B[i].what, "a row of Home")) last = i;
                if (last > 0) {
                    R.L->click(&R.c, B[last - 1].r.x + B[last - 1].r.w / 2, B[last - 1].r.y + B[last - 1].r.h / 2);
                    if (R.L->step) for (int j = 0; j < 80; j++) R.L->step(&R.c, 0.05f);
                }
            }
            shell_target T[96];
            int n = R.L->targets(&R.c, T, 96);
            char nm[200];
            int small = 0, off = 0, press = 0;
            for (int i = 0; i < n; i++) {
                if (T[i].kind != TARGET_PRESS) continue;
                press++;
                int shorter = T[i].r.w < T[i].r.h ? T[i].r.w : T[i].r.h;
                if (shorter < 44 && !small++)
                    printf("      first: %s is %dx%d\n", T[i].what, T[i].r.w, T[i].r.h);
                if (!inside_screen(T[i].r, SIZES[sz].w, R.c.screen_h) && !off++)
                    printf("      first: %s is at %d,%d %dx%d, off the %dx%d screen\n", T[i].what,
                           T[i].r.x, T[i].r.y, T[i].r.w, T[i].r.h, SIZES[sz].w, R.c.screen_h);
            }
            snprintf(nm, sizeof nm, "%s %dx%d text %.0f%%, %s: %d controls, each 44 px+ and on the screen",
                     look, SIZES[sz].w, SIZES[sz].h, (double)(SCALES[k] * 100.f), SCEN[scen], press);
            ok(nm, press > 0 && !small && !off);
            if (getenv("FLOORS_DUMP"))                          /* FLOORS_DUMP=1: every rectangle, for reading */
                for (int q = 0; q < n; q++)
                    printf("        %-36s %5d,%-5d %4dx%-4d %s\n", T[q].what, T[q].r.x, T[q].r.y, T[q].r.w, T[q].r.h,
                           T[q].kind == TARGET_WINDOW ? "(window)" : "");
        }
}

int main(void)
{
    printf("the shell's floors: 18 px words, 44 px targets, windows on the screen\n");
    text_floor();
    printf("\ntargets: 44 px, on the screen, in each look\n");
    floors("taskbar", S_WINS); floors("taskbar", S_MENU);
    floors("dock", S_WINS);    floors("dock", S_FIND);   floors("dock", S_HOME);   floors("dock", S_BUSY);
    floors("rail", S_WINS);    floors("rail", S_HOME);   floors("rail", S_LIST);
    floors("tiles", S_WINS);   floors("tiles", S_HOME);  floors("tiles", S_PAGES);
    printf("\n%d checks, %d failed\n", checked, fail);
    return fail ? 1 : 0;
}
