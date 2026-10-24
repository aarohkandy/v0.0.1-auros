/* handover-mount.h — the checked mount-move transaction used by stage_switch_root. */
#ifndef AUROS_HANDOVER_MOUNT_H
#define AUROS_HANDOVER_MOUNT_H

#include <stddef.h>

typedef struct {
    const char *from;
    const char *to;
} handover_mount;

typedef int (*handover_mount_fn)(const char *from, const char *to, void *ud);
typedef int (*handover_cleanup_fn)(void *ud);

/* Move every mount in order. On the first failure, move every completed mount back in reverse
 * order, then clean up the surrounding handover state. Returns 0 on success, -1 when the failed
 * move was fully rolled back, and -2 when rollback or cleanup failed and the caller must reboot. */
static inline int handover_move_transaction(const handover_mount *moves, size_t n,
                                            handover_mount_fn move,
                                            handover_mount_fn rollback,
                                            handover_cleanup_fn cleanup,
                                            void *ud)
{
    size_t done = 0;
    for (; done < n; done++)
        if (move(moves[done].from, moves[done].to, ud) != 0)
            break;
    if (done == n) return 0;

    int broken = 0;
    while (done > 0) {
        done--;
        if (rollback(moves[done].to, moves[done].from, ud) != 0)
            broken = 1;
    }
    if (cleanup && cleanup(ud) != 0) broken = 1;
    return broken ? -2 : -1;
}

#endif
