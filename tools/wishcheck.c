/* wishcheck.c — run wish documents through the real validator.
 *
 *   wishcheck [--sensors compass,tilt,light] [--apps DIR] [--themes DIR]
 *             [--said "her words"]      one JSON document per input line
 *
 * Prints, per line, "ok <sentence>" or "no <missing> <why>". Used by
 * tools/wishtest-schema.py to prove the engine accepts every shape the
 * shared schema (docs/wish-v1.schema.json) allows. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wish.h"

int main(int argc, char **argv)
{
    wish_env env = { .sensors = 0, .fg = 0xF3F7FD };
    const char *said = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sensors") && i + 1 < argc) {
            char *l = strdup(argv[++i]);
            for (char *t = strtok(l, ","); t; t = strtok(NULL, ","))
                env.sensors |= !strcmp(t, "compass") ? 1u << S_COMPASS :
                               !strcmp(t, "tilt") ? 1u << S_TILT : 1u << S_LIGHT;
            free(l);
        } else if (!strcmp(argv[i], "--apps") && i + 1 < argc) env.apps_dirs = argv[++i];
        else if (!strcmp(argv[i], "--themes") && i + 1 < argc) env.themes_dir = argv[++i];
        else if (!strcmp(argv[i], "--said") && i + 1 < argc) said = argv[++i];
    }
    env.said = said;
    static char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strcspn(line, "\n");
        wish w;
        wish_err e;
        if (wish_parse_text(line, n, &env, &w, &e) == 0) {
            char s[1024];
            wish_sentence(&w, s, sizeof s);
            printf("ok %s\n", s);
        } else printf("no %s %s\n", e.missing ? e.missing : "?", e.why ? e.why : "?");
    }
    return 0;
}
