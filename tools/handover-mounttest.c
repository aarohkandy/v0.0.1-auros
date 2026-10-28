#include <stdio.h>
#include <string.h>

#include "handover-mount.h"

typedef struct {
    int fail_move;
    int fail_rollback;
    int fail_cleanup;
    int moves;
    int rollbacks;
    int cleanups;
    int wiped;
    char trace[256];
    size_t trace_n;
} fixture;

static void note(fixture *f, char kind, const char *path)
{
    int n = snprintf(f->trace + f->trace_n, sizeof f->trace - f->trace_n,
                     "%c:%s ", kind, path);
    if (n > 0 && (size_t)n < sizeof f->trace - f->trace_n)
        f->trace_n += (size_t)n;
}

static int move_mount(const char *from, const char *to, void *ud)
{
    fixture *f = ud;
    (void)to;
    note(f, 'M', from);
    int this_move = f->moves++;
    return this_move == f->fail_move ? -1 : 0;
}

static int roll_mount_back(const char *from, const char *to, void *ud)
{
    fixture *f = ud;
    (void)from;
    note(f, 'R', to);
    int this_rollback = f->rollbacks++;
    return this_rollback == f->fail_rollback ? -1 : 0;
}

static int cleanup_handover(void *ud)
{
    fixture *f = ud;
    f->cleanups++;
    note(f, 'C', "newroot");
    return f->fail_cleanup ? -1 : 0;
}

static int check(int yes, const char *name, const fixture *f)
{
    if (yes) {
        printf("ok   %s\n", name);
        return 0;
    }
    printf("FAIL %s\n     %s\n", name, f->trace);
    return 1;
}

int main(void)
{
    static const handover_mount mounts[] = {
        { "/dev", "/newroot/dev" },
        { "/proc", "/newroot/proc" },
        { "/sys", "/newroot/sys" },
        { "/run", "/newroot/run" },
    };
    int failed = 0;

    for (int stop = 0; stop < 4; stop++) {
        fixture f = { .fail_move = stop, .fail_rollback = -1 };
        int rc = handover_move_transaction(mounts, 4, move_mount, roll_mount_back,
                                           cleanup_handover, &f);
        char name[100];
        snprintf(name, sizeof name, "move %d failure rolls back %d completed moves", stop, stop);
        failed += check(rc == -1 && f.moves == stop + 1 && f.rollbacks == stop &&
                        f.cleanups == 1 && !f.wiped, name, &f);
        char want[256] = "";
        size_t used = 0;
        for (int i = 0; i <= stop; i++)
            used += (size_t)snprintf(want + used, sizeof want - used,
                                     "M:%s ", mounts[i].from);
        for (int i = stop - 1; i >= 0; i--)
            used += (size_t)snprintf(want + used, sizeof want - used,
                                     "R:%s ", mounts[i].from);
        snprintf(want + used, sizeof want - used, "C:newroot ");
        failed += check(strcmp(f.trace, want) == 0,
                        "rollback order is exact and cleanup is last", &f);
    }

    fixture good = { .fail_move = -1, .fail_rollback = -1 };
    int rc = handover_move_transaction(mounts, 4, move_mount, roll_mount_back,
                                       cleanup_handover, &good);
    if (rc == 0) good.wiped = 1; /* exactly where boot.c may begin deleting initramfs */
    failed += check(rc == 0 && good.moves == 4 && good.rollbacks == 0 &&
                    good.cleanups == 0 && good.wiped,
                    "initramfs wipe is reachable only after every move succeeds", &good);

    fixture broken = { .fail_move = 3, .fail_rollback = 1 };
    rc = handover_move_transaction(mounts, 4, move_mount, roll_mount_back,
                                   cleanup_handover, &broken);
    failed += check(rc == -2 && broken.rollbacks == 3 && broken.cleanups == 1 && !broken.wiped,
                    "one failed rollback still attempts every rollback and requires reboot", &broken);

    fixture dirty = { .fail_move = 2, .fail_rollback = -1, .fail_cleanup = 1 };
    rc = handover_move_transaction(mounts, 4, move_mount, roll_mount_back,
                                   cleanup_handover, &dirty);
    failed += check(rc == -2 && dirty.rollbacks == 2 && dirty.cleanups == 1 && !dirty.wiped,
                    "failed special-mount/newroot cleanup requires reboot", &dirty);

    printf("%d failed\n", failed);
    return failed != 0;
}
