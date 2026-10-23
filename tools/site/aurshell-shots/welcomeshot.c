/* welcomeshot.c -- main.c's --png path plus the welcome panel, which
 * --png never opens. Usage: welcomeshot CONF SHELL OUT.png W H
 * Build with -DWELCOME_STATE=... pointing at a file holding phase=asking. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"
#include "foot.h"
#include "welcome.h"
#include "../common/wall.h"
#include "../common/png.h"

int main(int argc, char **argv)
{
    if (argc != 6) { fputs("usage: welcomeshot CONF SHELL OUT W H\n", stderr); return 2; }
    int w = atoi(argv[4]), h = atoi(argv[5]);
    theme_t t = {0};
    if (theme_load(&t, argv[1]) < 0) { perror(argv[1]); return 1; }
    shell_ctx c; memset(&c, 0, sizeof c);
    shell_theme_load(&c, &t);
    c.allow_install = c.allow_settings = c.allow_theme_change = c.allow_tty = c.allow_network = 1;
    if (shell_archetype_load(&c, argv[2]) < 0) { perror(argv[2]); return 1; }
    c.text_scale = foot_load_text_scale(); c.foot_hover = -1;
    shell_seed_apps(&c); shell_scan_apps(&c);
    c.mouse_x = c.mouse_y = -1; c.hover = c.focus = -1;
    const shell_layout *L = shell_layout_by_id(c.layout_id);
    shell_fonts f; shell_fonts_load(&f, &c);
    surface *s = surface_new(w, h), *wall = surface_new(w, h);
    uint32_t *tmp = malloc((size_t)w * h * 4);
    wall_render(tmp, w, h, &t);
    for (int i = 0; i < w * h; i++) wall->px[i] = 0xFF000000u | tmp[i];
    c.screen_w = w; c.screen_h = h - foot_height(&c);
    if (L->init) L->init(&c);
    if (!welcome_init(&c)) { fputs("welcome did not open (state file?)\n", stderr); return 1; }
    draw_track_reset();
    surface body = *s; body.h = c.screen_h;
    L->paint(&c, &body, &f, wall);
    welcome_paint(&c, s, &f);
    foot_paint(&c, s, &f);
    for (int i = 0; i < w * h; i++) tmp[i] = s->px[i] & 0xFFFFFFu;
    return png_write_rgb(argv[3], tmp, w, h);
}
