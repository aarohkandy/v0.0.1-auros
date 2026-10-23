/* textsystest.c — does "Bigger" reach her programs?
 *
 * It did not: the band wrote a file only the shell read, and Internet,
 * the store and every document stayed as small as before while the Help
 * panel said the words change "everywhere". This checks the two things
 * the shell now does with her size (src/aurshell/textsys.c): the GTK
 * settings.ini it writes, and the command it starts -- and that main.c
 * does both when she presses the button and when the desktop starts.
 *
 *   cc -O2 -std=gnu11 -o /tmp/textsystest tools/textsystest.c \
 *      src/aurshell/textsys.c src/aurshell/run.c -I src/aurshell && /tmp/textsystest
 *   (from the repository root)
 *
 * What it cannot see: whether Firefox on a booted image follows the
 * setting. That is tools/profiletest.sh's boot, and is listed as open in
 * docs/handoff/streams/C.md.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/aurshell/textsys.h"

static int fail, checked;
static void ok(const char *what, int good)
{
    checked++;
    printf("    %-68s %s\n", what, good ? "ok" : "FAIL");
    if (!good) fail++;
}

static char *slurp(const char *p)
{
    static char b[4096];
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    return strdup(b);          /* a copy: two files are read at once */
}

static int source_count(const char *needle)
{
    static char buf[600000];
    FILE *f = fopen("src/aurshell/main.c", "r");
    if (!f) { perror("src/aurshell/main.c"); exit(2); }
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    int k = 0;
    for (const char *p = buf; (p = strstr(p, needle)); p += strlen(needle)) k++;
    return k;
}

static int source_has(const char *needle)
{
    static char buf[600000];
    FILE *f = fopen("src/aurshell/main.c", "r");
    if (!f) { perror("src/aurshell/main.c"); exit(2); }
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    char home[] = "/tmp/textsystest-XXXXXX";
    if (!mkdtemp(home)) { perror("mkdtemp"); return 2; }
    printf("\n\"Bigger\" for her programs\n\n");

    ok("100% is GTK's own 96 dpi", textsys_xft_dpi(1.0f) == 98304);
    ok("150% is 1.5 times that", textsys_xft_dpi(1.5f) == 147456);
    ok("past the largest she can choose is the largest", textsys_xft_dpi(9.f) == textsys_xft_dpi(2.f));
    ok("NaN is no change", textsys_xft_dpi(0.f / 0.f) == 98304);

    char buf[16]; const char *argv[6];
    textsys_gsettings_argv(1.35f, buf, sizeof buf, argv);
    ok("the command is gsettings set ... text-scaling-factor 1.35",
       !strcmp(argv[0], "gsettings") && !strcmp(argv[1], "set") &&
       !strcmp(argv[2], "org.gnome.desktop.interface") &&
       !strcmp(argv[3], "text-scaling-factor") && !strcmp(argv[4], "1.35") && !argv[5]);

    /* GTK 3 and 4, a file with other things in it, no trailing newline. */
    char d3[300], f3[340], f4[340];
    snprintf(d3, sizeof d3, "%s/.config/gtk-3.0", home);
    snprintf(f3, sizeof f3, "%s/settings.ini", d3);
    snprintf(f4, sizeof f4, "%s/.config/gtk-4.0/settings.ini", home);
    { char c1[300]; snprintf(c1, sizeof c1, "%s/.config", home); mkdir(c1, 0755); mkdir(d3, 0755); }
    FILE *f = fopen(f3, "w");
    fputs("[Settings]\ngtk-font-name=Inter 11\ngtk-xft-dpi=1\ngtk-application-prefer-dark-theme=0", f);
    fclose(f);
    ok("it writes both GTK settings files", textsys_write_ini(home, 1.5f) == 0);
    char *t3 = slurp(f3), *t4 = slurp(f4);
    ok("GTK 3 gets the new resolution", t3 && strstr(t3, "gtk-xft-dpi=147456\n"));
    ok("...replacing the old one, not adding a second",
       t3 && strstr(t3, "gtk-xft-dpi=1\n") == NULL &&
       strstr(strstr(t3, "gtk-xft-dpi=") + 10, "gtk-xft-dpi=") == NULL);
    ok("...and keeps everything else she had (font, dark/light)",
       t3 && strstr(t3, "gtk-font-name=Inter 11\n") &&
       strstr(t3, "gtk-application-prefer-dark-theme=0\n"));
    ok("GTK 4 gets it too, in a file it created",
       t4 && strstr(t4, "[Settings]\n") && strstr(t4, "gtk-xft-dpi=147456\n"));
    textsys_write_ini(home, 1.0f);
    t3 = slurp(f3);
    ok("pressing Smaller back to 100% writes 100%, once",
       t3 && strstr(t3, "gtk-xft-dpi=98304\n") &&
       strstr(strstr(t3, "gtk-xft-dpi=") + 10, "gtk-xft-dpi=") == NULL);
    ok("a home that is not a path is refused", textsys_write_ini("", 1.5f) < 0);

    printf("\n  and the shell does it\n");
    ok("main.c applies it when she presses Bigger or Smaller",
       source_has("textsys_apply(c.text_scale);      /* and her programs' */"));
    ok("...and again when the desktop starts at a size she chose",
       source_has("textsys_apply(c.text_scale);\n"));

    printf("\n  the buttons on her programs' own title bars are her look's\n");
    ok("Like Windows: minimise, maximise, close",
       !strcmp(textsys_buttons_for("taskbar"), ":minimize,maximize,close"));
    ok("Like a Mac: minimise and close (its windows have no maximise)",
       !strcmp(textsys_buttons_for("dock"), ":minimize,close"));
    ok("Big and simple and One thing at a time: none (Close this is in the footer)",
       !strcmp(textsys_buttons_for("rail"), ":") && !strcmp(textsys_buttons_for("tiles"), ":"));
    ok("a look it does not know has none", !strcmp(textsys_buttons_for("nope"), ":") &&
                                            !strcmp(textsys_buttons_for(NULL), ":"));
    ok("it writes both GTK settings files", textsys_write_buttons(home, "taskbar") == 0);
    t3 = slurp(f3); t4 = slurp(f4);
    ok("GTK 3 gets the layout, beside the size she chose",
       t3 && strstr(t3, "gtk-decoration-layout=:minimize,maximize,close\n") &&
       strstr(t3, "gtk-xft-dpi=98304\n"));
    ok("GTK 4 gets it too", t4 && strstr(t4, "gtk-decoration-layout=:minimize,maximize,close\n"));
    textsys_write_buttons(home, "tiles");
    t3 = slurp(f3);
    ok("changing her look replaces it, once (and keeps the rest)",
       t3 && strstr(t3, "gtk-decoration-layout=:\n") &&
       strstr(t3, "gtk-decoration-layout=:minimize") == NULL &&
       strstr(t3, "gtk-font-name=Inter 11\n") && strstr(t3, "gtk-xft-dpi=98304\n"));
    ok("a home that is not a path is refused", textsys_write_buttons("", "taskbar") < 0);
    ok("main.c sets them when the desktop starts and when her look changes",
       source_count("textsys_buttons(L->id);") == 2);

    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", home);
    printf("\n");
    if (fail) { printf("%d of %d wrong.\n", fail, checked); return 1; }
    printf("%d checks: her size reaches the toolkit, and the shell sends it.\n", checked);
    return 0;
}
