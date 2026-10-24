#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "handover-mount.h"
#include "screen-stop.h"
#include "transient-mount.h"

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

typedef struct {
    int signals;
    int waits;
    int pauses;
    int reap_after_kill;
    int never_reap;
    int wait_error;
    int kill_error;
} screen_fixture;

static int fake_signal(pid_t pid, int sig, void *ud)
{
    screen_fixture *f = ud;
    (void)pid;
    f->signals |= sig == SIGTERM ? 1 : 2;
    if (sig == SIGKILL && f->kill_error) { errno = EPERM; return -1; }
    return 0;
}

static pid_t fake_wait(pid_t pid, int *status, int flags, void *ud)
{
    screen_fixture *f = ud;
    (void)status; (void)flags;
    f->waits++;
    if (f->wait_error) { errno = EIO; return -1; }
    if (f->never_reap) return 0;
    if (f->reap_after_kill && !(f->signals & 2)) return 0;
    return pid;
}

static void fake_pause(void *ud)
{
    screen_fixture *f = ud;
    f->pauses++;
}

typedef struct {
    int fail_unmount;
    int fail_mountpoint_remove;
    int unmounts;
    int removes;
} transient_fixture;

typedef struct {
    int file_errno;
    int dir_errno;
    int files;
    int dirs;
} scratch_fixture;

static int fake_unmount(const char *path, void *ud)
{
    transient_fixture *f = ud;
    (void)path;
    f->unmounts++;
    if (f->fail_unmount) { errno = EBUSY; return -1; }
    return 0;
}

static int fake_remove(const char *path, void *ud)
{
    transient_fixture *f = ud;
    f->removes++;
    if (f->fail_mountpoint_remove && f->removes == 1) {
        errno = ENOTEMPTY;
        return -1;
    }
    if (!strcmp(path, "/run/aurstage")) {
        errno = ENOTEMPTY;
        return -1;
    }
    return 0;
}

static int fake_remove_file(const char *path, void *ud)
{
    scratch_fixture *f = ud;
    (void)path;
    f->files++;
    if (f->file_errno) { errno = f->file_errno; return -1; }
    return 0;
}

static int fake_remove_scratch_dir(const char *path, void *ud)
{
    scratch_fixture *f = ud;
    (void)path;
    f->dirs++;
    if (f->dir_errno) { errno = f->dir_errno; return -1; }
    return 0;
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

    screen_fixture term = { 0 };
    rc = screen_stop_process(42, 2, fake_signal, fake_wait, fake_pause, &term);
    failed += check(rc == 0 && term.signals == 1 && term.waits == 1,
                    "a screen reaped after SIGTERM needs no SIGKILL", &(fixture){0});

    screen_fixture killed = { .reap_after_kill = 1 };
    rc = screen_stop_process(42, 2, fake_signal, fake_wait, fake_pause, &killed);
    failed += check(rc == 0 && killed.signals == 3 && killed.waits == 3,
                    "a screen that ignores SIGTERM is reaped after SIGKILL", &(fixture){0});

    screen_fixture stuck = { .never_reap = 1 };
    rc = screen_stop_process(42, 2, fake_signal, fake_wait, fake_pause, &stuck);
    failed += check(rc == -1 && stuck.signals == 3 && stuck.waits == 4,
                    "an unreaped screen makes handover fail after SIGKILL", &(fixture){0});

    screen_fixture bad_wait = { .wait_error = 1 };
    rc = screen_stop_process(42, 2, fake_signal, fake_wait, fake_pause, &bad_wait);
    failed += check(rc == -1 && bad_wait.signals == 3 && bad_wait.waits == 2,
                    "a wait failure still tries SIGKILL, then makes handover fail", &(fixture){0});

    screen_fixture bad_kill = { .never_reap = 1, .kill_error = 1 };
    rc = screen_stop_process(42, 2, fake_signal, fake_wait, fake_pause, &bad_kill);
    failed += check(rc == -1 && bad_kill.signals == 3 && bad_kill.waits == 4,
                    "a failed SIGKILL cannot be mistaken for a stopped screen", &(fixture){0});

    int mounted = 1;
    transient_fixture busy = { .fail_unmount = 1 };
    rc = transient_mount_cleanup("/run/aurstage/esp", "/run/aurstage", &mounted,
                                 fake_unmount, fake_remove, &busy);
    failed += check(rc == TRANSIENT_UNMOUNT_FAILED && mounted == 1 &&
                    busy.unmounts == 1 && busy.removes == 0,
                    "an unmount failure keeps the mount live and blocks handover", &(fixture){0});

    transient_fixture clean = { 0 };
    rc = transient_mount_cleanup("/run/aurstage/esp", "/run/aurstage", &mounted,
                                 fake_unmount, fake_remove, &clean);
    failed += check(rc == 0 && mounted == 0 && clean.unmounts == 1 && clean.removes == 2,
                    "a retry releases the mount and removes its owned directory", &(fixture){0});

    mounted = 0;
    transient_fixture dirty_dir = { .fail_mountpoint_remove = 1 };
    rc = transient_mount_cleanup("/run/aurstage/windows", "/run/aurstage", &mounted,
                                 fake_unmount, fake_remove, &dirty_dir);
    failed += check(rc == TRANSIENT_MOUNTPOINT_FAILED && dirty_dir.unmounts == 0 &&
                    dirty_dir.removes == 1,
                    "a nonempty staging mountpoint blocks handover", &(fixture){0});

    scratch_fixture sensitive = { .file_errno = EACCES };
    rc = transient_scratch_cleanup("/run/aurstage/saved.bin", "/run/aurstage",
                                   fake_remove_file, fake_remove_scratch_dir, &sensitive);
    failed += check(rc == TRANSIENT_FILE_FAILED && sensitive.files == 1 && sensitive.dirs == 0,
                    "a saved.bin unlink failure blocks handover", &(fixture){0});

    scratch_fixture leftover = { .dir_errno = ENOTEMPTY };
    rc = transient_scratch_cleanup("/run/aurstage/saved.bin", "/run/aurstage",
                                   fake_remove_file, fake_remove_scratch_dir, &leftover);
    failed += check(rc == TRANSIENT_PARENT_FAILED && leftover.files == 1 && leftover.dirs == 1,
                    "unknown staging scratch blocks the /run handover", &(fixture){0});

    scratch_fixture absent = { .file_errno = ENOENT, .dir_errno = ENOENT };
    rc = transient_scratch_cleanup("/run/aurstage/saved.bin", "/run/aurstage",
                                   fake_remove_file, fake_remove_scratch_dir, &absent);
    failed += check(rc == TRANSIENT_CLEAN && absent.files == 1 && absent.dirs == 1,
                    "already-absent staging scratch is safe", &(fixture){0});

    printf("%d failed\n", failed);
    return failed != 0;
}
