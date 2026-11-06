/* closetest.c — a program closes. What is she looking at?
 *
 * Big and simple ("rail") draws a row of cards from its own slide
 * position, and nothing moved that position when a program left. Close
 * the last program and the row stayed where its card had been: an empty
 * screen with Home at the edge, and no word about what to press. Nothing
 * caught it, because every test opens programs and none closes one.
 *
 * Each look is driven through shell_win_gone(), the path a real program
 * takes on its way out, and asked what is in front through its own
 * targets(), the same list the pointer and the keyboard work from.
 *
 *   closetest [-q]     exit 0 if every look lands somewhere she can use
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/aurshell/shell.h"

#define W 1366
#define H  768

extern const shell_layout layout_rail, layout_tiles, layout_locked,
                          layout_taskbar, layout_dock, layout_workbench;

static const shell_layout *ALL[] = {
    &layout_rail, &layout_tiles, &layout_locked,
    &layout_taskbar, &layout_dock, &layout_workbench,
};
static const char *IDS[] = { "rail", "tiles", "locked", "taskbar", "dock", "workbench" };

static int fails, checks, quiet;

static void check(const char *look, const char *what, int ok, const char *why)
{
    checks++;
    if (!ok) { fails++; printf("  FAIL %-10s %s (%s)\n", look, what, why); }
    else if (!quiet) printf("  ok   %-10s %s\n", look, what);
}

static int spawn_nothing(shell_ctx *c, const char *argv_blob, int n_args)
{
    (void)c; (void)argv_blob; (void)n_args;
    return 0;
}

static void seed(shell_ctx *c, const char *id, int n_wins)
{
    memset(c, 0, sizeof *c);
    theme_t t = {0};
    shell_theme_load(c, &t);
    snprintf(c->layout_id, sizeof c->layout_id, "%s", id);
    c->screen_w = W; c->screen_h = H;
    c->show_clock = c->status_full = 1;
    c->allow_install = c->allow_settings = c->allow_theme_change = c->allow_tty = 1;
    c->hover = -1;
    c->focus = -1;
    c->spawn = spawn_nothing;
    static const char *NAMES[] = { "Internet", "Photos", "Your files", "Letters", "Music", "Solitaire" };
    c->n_apps = (int)(sizeof NAMES / sizeof NAMES[0]);
    for (int i = 0; i < c->n_apps; i++) {
        snprintf(c->apps[i].id,   sizeof c->apps[i].id,   "app%d", i);
        snprintf(c->apps[i].name, sizeof c->apps[i].name, "%s", NAMES[i]);
        snprintf(c->apps[i].hint, sizeof c->apps[i].hint, "Something to do");
        memcpy(c->apps[i].exec, "/usr/bin/x\0", 11);
        c->apps[i].n_args = 1;
        c->apps[i].pinned = i < 4;
    }
    (void)n_wins;
}

static void settle(shell_ctx *c, const shell_layout *L)
{
    for (int i = 0; i < 240; i++) if (L->step) L->step(c, 0.016f);
}

/* A real window, the way session_sync() takes one in. */
static void open_win(shell_ctx *c, const shell_layout *L, int app)
{
    int i = c->n_wins++;
    c->wins[i].app = app;
    c->wins[i].wid = (uint32_t)(100 + app);
    snprintf(c->wins[i].title, sizeof c->wins[i].title, "%s", c->apps[app].name);
    if (L->present) L->present(c, i); else c->focus = i;
    settle(c, L);
}

/* Windows in front, and how many stand at the side. A card in front is always
 * reported centred, so a row in the wrong place doesn't show there: it shows as
 * the only window left standing at the side, with nothing in front of it. */
static int windows_shown(shell_ctx *c, const shell_layout *L, int *beside)
{
    shell_target t[96];
    int n = L->targets ? L->targets(c, t, 96) : 0, w = 0;   /* locked and workbench list none */
    *beside = 0;
    for (int i = 0; i < n; i++) {
        if (t[i].kind == TARGET_WINDOW) w++;
        else if (strstr(t[i].what, "window's card")) (*beside)++;
    }
    return w;
}

int main(int argc, char **argv)
{
    quiet = argc > 1 && !strcmp(argv[1], "-q");
    static shell_ctx c;
    for (size_t k = 0; k < sizeof ALL / sizeof ALL[0]; k++) {
        const shell_layout *L = ALL[k];
        const char *id = IDS[k];
        int beside = 0, n = 0;
        /* A kiosk has no Home to land on: it starts its own program as it comes up,
         * and the machine does nothing else (layouts/locked.c). Nothing here applies. */
        if (L == &layout_locked) { printf("  (%s has no Home, so nothing here applies)\n", id); continue; }

        /* Her last program closes. */
        seed(&c, id, 0);
        if (L->init) L->init(&c);
        open_win(&c, L, 2);
        shell_win_gone(&c, L, 0);
        settle(&c, L);
        n = windows_shown(&c, L, &beside);
        check(id, "the last program closes: nothing is left on the screen as a window", n == 0, "a window is still in front");
        check(id, "...and Home is what is in front", c.focus == -1, "the focus names a program");
        if (L->fini) L->fini(&c);

        /* She closes a program that has not opened yet: there is no window behind the slot. */
        seed(&c, id, 0);
        if (L->init) L->init(&c);
        open_win(&c, L, 2);
        c.wins[0].wid = 0;
        c.wins[0].starting = 1;
        shell_win_gone(&c, L, 0);
        settle(&c, L);
        n = windows_shown(&c, L, &beside);
        check(id, "a program that was still starting closes: nothing is left of it", n == 0 && c.focus == -1 && c.n_wins == 0, "it is still in front");
        if (L->fini) L->fini(&c);

        /* The one in front closes, and another is still open. */
        seed(&c, id, 0);
        if (L->init) L->init(&c);
        open_win(&c, L, 1);
        open_win(&c, L, 2);
        shell_win_gone(&c, L, 1);
        settle(&c, L);
        n = windows_shown(&c, L, &beside);
        check(id, "the front program closes: the other one comes forward", (!L->targets || n == 1) && c.focus == 0, "no window in front");
        if (L == &layout_rail) check(id, "...and the row has moved to it", beside == 0, "it is still at the side");
        if (L->fini) L->fini(&c);

        /* One behind her closes: what she is looking at stays where it is. */
        seed(&c, id, 0);
        if (L->init) L->init(&c);
        open_win(&c, L, 1);
        open_win(&c, L, 2);
        shell_win_gone(&c, L, 0);
        settle(&c, L);
        n = windows_shown(&c, L, &beside);
        check(id, "another program closes: the one she is using stays in front", (!L->targets || n == 1) && c.focus == 0, "it moved");
        if (L == &layout_rail) check(id, "...and the row has followed it down", beside == 0, "it is left at the side");
        /* tiles remembers which slot is on the screen, and that slot just moved down.
         * It only obeys a window that asks to go away if it is the one on the screen. */
        if (L == &layout_tiles) {
            L->request(&c, 0, WREQ_MINIMIZE);
            settle(&c, L);
            n = windows_shown(&c, L, &beside);
            check(id, "...and it still knows which one that is", n == 0, "it lost count of its slot");
        }
        if (L->fini) L->fini(&c);
    }
    printf("closetest: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
