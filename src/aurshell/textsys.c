/* textsys.c — see textsys.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "textsys.h"
#include "run.h"

static float clamp_scale(float s)
{
    if (!(s >= 0.8f)) return s != s ? 1.f : 0.8f;     /* NaN is "no change" */
    return s > 2.0f ? 2.0f : s;
}

int textsys_xft_dpi(float scale)
{
    return (int)(96.f * 1024.f * clamp_scale(scale) + 0.5f);
}

void textsys_gsettings_argv(float scale, char *buf, size_t n, const char *argv[6])
{
    snprintf(buf, n, "%.2f", (double)clamp_scale(scale));
    argv[0] = "gsettings";
    argv[1] = "set";
    argv[2] = "org.gnome.desktop.interface";
    argv[3] = "text-scaling-factor";
    argv[4] = buf;
    argv[5] = NULL;
}

int textsys_cursor_px(float scale)
{
    float ts = clamp_scale(scale);
    int sc = (int)(2.f * (ts < 1.f ? 1.f : ts) + 0.5f);     /* cursor.c's cursor_scale() */
    if (sc < 2) sc = 2;
    if (sc > 4) sc = 4;
    return 24 * sc;
}

void textsys_cursor_argv(float scale, char *buf, size_t n, const char *argv[6])
{
    snprintf(buf, n, "%d", textsys_cursor_px(scale));
    argv[0] = "gsettings";
    argv[1] = "set";
    argv[2] = "org.gnome.desktop.interface";
    argv[3] = "cursor-size";
    argv[4] = buf;
    argv[5] = NULL;
}

/* One `key=value` line replaced or added under [Settings]; the rest of
 * the file is exactly as it was. */
static int ini_set(const char *path, const char *key, const char *value)
{
    char text[8192];
    size_t w = 0, kl = strlen(key);
    int seen = 0, group = 0;
    text[0] = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f) && w + 700 < sizeof text) {
            if (!strncmp(line, "[Settings]", 10)) group = 1;
            if (group && !strncmp(line, key, kl) && line[kl] == '=') {
                if (!seen)
                    w += (size_t)snprintf(text + w, sizeof text - w, "%s=%s\n", key, value);
                seen = 1;
                continue;
            }
            w += (size_t)snprintf(text + w, sizeof text - w, "%s", line);
        }
        fclose(f);
    }
    /* A file with no trailing newline must not glue the next line on. */
    if (w && text[w - 1] != '\n') text[w++] = '\n';
    if (!group) w += (size_t)snprintf(text + w, sizeof text - w, "[Settings]\n");
    if (!seen) w += (size_t)snprintf(text + w, sizeof text - w, "%s=%s\n", key, value);
    /* Written beside it and renamed over it, so a power cut leaves her
     * old file or her new one and not half of either. */
    char tmp[720];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *o = fopen(tmp, "w");
    if (!o) return -1;
    int ok = fwrite(text, 1, w, o) == w;
    ok = (fclose(o) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int textsys_write_ini(const char *home, float scale)
{
    if (!home || !*home || strlen(home) > 400) return -1;
    char dir[600], path[700], val[24];
    snprintf(val, sizeof val, "%d", textsys_xft_dpi(scale));
    int bad = 0;
    for (int v = 3; v <= 4; v++) {
        snprintf(dir, sizeof dir, "%s/.config", home);
        mkdir(dir, 0755);
        snprintf(dir, sizeof dir, "%s/.config/gtk-%d.0", home, v);
        if (mkdir(dir, 0755) < 0 && errno != EEXIST) { bad = 1; continue; }
        snprintf(path, sizeof path, "%s/settings.ini", dir);
        if (ini_set(path, "gtk-xft-dpi", val) < 0) bad = 1;
    }
    return bad ? -1 : 0;
}

void textsys_apply(float scale)
{
    const char *home = getenv("HOME");
    if (home && *home) (void)textsys_write_ini(home, scale);
    char buf[16];
    const char *argv[6];
    textsys_gsettings_argv(scale, buf, sizeof buf, argv);
    (void)run_detached(argv);
    char cbuf[16];
    textsys_cursor_argv(scale, cbuf, sizeof cbuf, argv);
    (void)run_detached(argv);
}

const char *textsys_buttons_for(const char *id)
{
    if (id && !strcmp(id, "taskbar")) return ":minimize,maximize,close";
    if (id && !strcmp(id, "dock"))    return ":minimize,close";
    return ":";
}

int textsys_write_buttons(const char *home, const char *id)
{
    if (!home || !*home || strlen(home) > 400) return -1;
    const char *v = textsys_buttons_for(id);
    char dir[600], path[700];
    int bad = 0;
    for (int g = 3; g <= 4; g++) {
        snprintf(dir, sizeof dir, "%s/.config", home);
        mkdir(dir, 0755);
        snprintf(dir, sizeof dir, "%s/.config/gtk-%d.0", home, g);
        if (mkdir(dir, 0755) < 0 && errno != EEXIST) { bad = 1; continue; }
        snprintf(path, sizeof path, "%s/settings.ini", dir);
        if (ini_set(path, "gtk-decoration-layout", v) < 0) bad = 1;
    }
    return bad ? -1 : 0;
}

void textsys_buttons(const char *id)
{
    const char *home = getenv("HOME");
    if (home && *home) (void)textsys_write_buttons(home, id);
    const char *argv[] = { "gsettings", "set", "org.gnome.desktop.wm.preferences",
                           "button-layout", textsys_buttons_for(id), NULL };
    (void)run_detached(argv);
}
