/* appstest.c — which programs she is shown, and which four sit on her dock.
 *
 * Two things the scan (src/aurshell/apps.c) decides that she can only see the
 * result of:
 *
 *   1. A tool she should never meet is hidden by a copy of its desktop entry
 *      that says NoDisplay=true, in /usr/local/share/applications (build/forge
 *      writes one for each). That directory is searched AFTER /usr/share, and
 *      a later copy of an entry replaces the earlier one -- but a copy that was
 *      refused was simply skipped, so the original stayed on her list. "LibreOffice
 *      Math" sat in "All my programs" beside Letters for exactly that reason.
 *   2. The first four programs are her dock's favourites. Letters (a word
 *      processor) tied with Documents, Notes and Spreadsheets and sorted among
 *      them by its first letter, so with an email program installed it was the
 *      fifth and was not on the dock.
 *
 * Machines are made of folders of desktop entries (AUROS_APP_DIRS, as
 * tools/storetest.c does), searched in the order given, last wins.
 *
 *   cc -O2 -std=gnu11 -o /tmp/appstest tools/appstest.c src/aurshell/apps.c \
 *      src/aurshell/draw.c src/aurshell/shellcommon.c src/aurshell/anim.c \
 *      src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
 *      -I src/aurshell -I src/common -lm && /tmp/appstest
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../src/aurshell/shell.h"

static int fails, checks;
static void ok(const char *what, int cond)
{
    checks++;
    if (!cond) fails++;
    printf("  %-70s %s\n", what, cond ? "ok" : "FAIL");
}

static void entry(const char *dir, const char *id, const char *name, const char *cats, const char *extra)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s.desktop", dir, id);
    FILE *f = fopen(p, "w");
    if (!f) { perror(p); exit(2); }
    fprintf(f, "[Desktop Entry]\nType=Application\nName=%s\nCategories=%s\nExec=true %s\n%s",
            name, cats, id, extra ? extra : "");
    fclose(f);
}

static int shown(const shell_ctx *c, const char *name)
{
    for (int i = 0; i < c->n_apps; i++) if (!strcmp(c->apps[i].name, name)) return 1;
    return 0;
}
static int pinned(const shell_ctx *c, const char *name)
{
    for (int i = 0; i < c->n_apps; i++)
        if (!strcmp(c->apps[i].name, name)) return c->apps[i].pinned;
    return 0;
}
static int index_of(const shell_ctx *c, const char *name)
{
    for (int i = 0; i < c->n_apps; i++) if (!strcmp(c->apps[i].name, name)) return i;
    return -1;
}

static char sys[300], loc[300], envv[700];

static void fresh(const char *base)
{
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/sys '%s'/loc", base, base);
    if (system(cmd)) exit(2);
    snprintf(sys, sizeof sys, "%s/sys", base);
    snprintf(loc, sizeof loc, "%s/loc", base);
    mkdir(sys, 0755); mkdir(loc, 0755);
    snprintf(envv, sizeof envv, "%s:%s", sys, loc);
    setenv("AUROS_APP_DIRS", envv, 1);
}

int main(void)
{
    char tmpl[] = "/tmp/auros-appstest-XXXXXX";
    char *base = mkdtemp(tmpl);
    if (!base) { perror("mkdtemp"); return 2; }
    setenv("HOME", base, 1);

    printf("a tool hidden by a later copy of its entry\n");
    fresh(base);
    entry(sys, "libreoffice-math", "LibreOffice Math", "Office;Math;", NULL);
    entry(sys, "thunar-bulk-rename", "Bulk Rename", "Utility;FileTools;", NULL);
    entry(sys, "libreoffice-writer", "LibreOffice Writer", "Office;WordProcessor;", NULL);
    /* build/forge: the packager's words replaced, and two launchers hidden. */
    entry(loc, "libreoffice-writer", "Letters", "Office;WordProcessor;", NULL);
    entry(loc, "libreoffice-math", "LibreOffice Math", "Office;Math;", "NoDisplay=true\n");
    entry(loc, "thunar-bulk-rename", "Bulk Rename", "Utility;FileTools;", "Hidden=true\n");
    entry(sys, "firefox", "Internet", "Network;WebBrowser;", NULL);
    {
        shell_ctx c; memset(&c, 0, sizeof c);
        shell_scan_apps(&c);
        ok("a copy that says NoDisplay hides the program", !shown(&c, "LibreOffice Math"));
        ok("...and so does one that says Hidden", !shown(&c, "Bulk Rename"));
        ok("a renamed copy still replaces the original (Letters, once)", shown(&c, "Letters") && !shown(&c, "LibreOffice Writer"));
        ok("the rest of the machine is untouched (Internet is shown)", shown(&c, "Internet"));
    }
    /* The other way round: hidden FIRST (in the system directory), shown by a later copy,
     * is a program she chose to show; the later copy wins. */
    fresh(base);
    entry(sys, "thing", "Old Thing", "Utility;", "NoDisplay=true\n");
    entry(loc, "thing", "New Thing", "Utility;", NULL);
    {
        shell_ctx c; memset(&c, 0, sizeof c);
        shell_scan_apps(&c);
        ok("a later copy that is shown wins over an earlier one that was not", shown(&c, "New Thing"));
    }

    printf("\nthe four on her dock\n");
    /* Letters, Documents (a PDF reader: Office, not a word processor), Notes, Spreadsheets,
     * the browser, her files and an email program. */
    fresh(base);
    entry(sys, "firefox", "Internet", "Network;WebBrowser;", NULL);
    entry(sys, "thunar", "Your files", "System;FileTools;FileManager;", NULL);
    entry(sys, "thunderbird", "Email", "Network;Email;", NULL);
    entry(sys, "org.gnome.Evince", "Documents", "GNOME;GTK;Office;Viewer;Graphics;", NULL);
    entry(sys, "libreoffice-writer", "Letters", "Office;WordProcessor;", NULL);
    entry(sys, "libreoffice-calc", "Spreadsheets", "Office;Spreadsheet;", NULL);
    entry(sys, "org.xfce.mousepad", "Notes", "Utility;TextEditor;", NULL);
    {
        shell_ctx c; memset(&c, 0, sizeof c);
        shell_scan_apps(&c);
        ok("Letters is one of the four", pinned(&c, "Letters"));
        ok("...so Documents, Notes and Spreadsheets are not", !pinned(&c, "Documents") && !pinned(&c, "Notes") && !pinned(&c, "Spreadsheets"));
        ok("Internet, Your files and Email still come first", pinned(&c, "Internet") && pinned(&c, "Your files") && pinned(&c, "Email"));
        ok("Letters comes before Documents, Notes and Spreadsheets in the list",
           index_of(&c, "Letters") < index_of(&c, "Documents") && index_of(&c, "Letters") < index_of(&c, "Notes") &&
           index_of(&c, "Letters") < index_of(&c, "Spreadsheets"));
    }

    printf("\nappstest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
