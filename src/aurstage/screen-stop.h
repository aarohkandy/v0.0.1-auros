/* screen-stop.h -- bounded, testable shutdown of the staging progress screen. */
#ifndef AUROS_SCREEN_STOP_H
#define AUROS_SCREEN_STOP_H

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef int (*screen_signal_fn)(pid_t pid, int sig, void *ud);
typedef pid_t (*screen_wait_fn)(pid_t pid, int *status, int flags, void *ud);
typedef void (*screen_pause_fn)(void *ud);

static inline int screen_wait_reaped(pid_t pid, int attempts,
                                     screen_wait_fn wait_child,
                                     screen_pause_fn pause_once, void *ud)
{
    for (int i = 0; i < attempts; i++) {
        pid_t got = wait_child(pid, NULL, WNOHANG, ud);
        if (got == pid || (got < 0 && errno == ECHILD)) return 0;
        if (got < 0 && errno != EINTR) return -1;
        pause_once(ud);
    }
    return 1;
}

/* Success means the child was reaped, or wait reported that it is no longer
 * our child. A signal being sent is not success: an uninterruptible child can
 * survive SIGKILL and retain the display device. */
static inline int screen_stop_process(pid_t pid, int attempts,
                                      screen_signal_fn send_signal,
                                      screen_wait_fn wait_child,
                                      screen_pause_fn pause_once, void *ud)
{
    (void)send_signal(pid, SIGTERM, ud);
    int waited = screen_wait_reaped(pid, attempts, wait_child, pause_once, ud);
    if (waited == 0) return 0;

    (void)send_signal(pid, SIGKILL, ud);
    waited = screen_wait_reaped(pid, attempts, wait_child, pause_once, ud);
    return waited == 0 ? 0 : -1;
}

/* WHAT THE CONSOLE SAYS WHEN THE PROGRESS SCREEN HAS LET GO AND AUROS'S OWN HAS NOT YET STARTED.
 * The display then shows the console, which is every line aurstage said, in the kernel's small
 * type: log lines, a start-up entry number, a device name, "verdict=installed" (real Windows 11
 * run, 2026-11-04, ten seconds or more on a fast disk). This clears them and says what is
 * happening in her words. It is written straight to the console, not through stage_say: it is
 * for her, not for the log. Nothing in it is a name, a path or a number
 * (tools/handover-mounttest.c). */
#define HANDOVER_NOTICE \
    "\033[2J\033[H\n" \
    "  AurOS is installed. Starting it now.\n\n" \
    "  This can take a minute. Please don't press anything.\n"

#endif
