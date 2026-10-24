/* transient-mount.h -- release one staging-only mount before /run is carried. */
#ifndef AUROS_TRANSIENT_MOUNT_H
#define AUROS_TRANSIENT_MOUNT_H

#include <errno.h>

typedef int (*transient_path_fn)(const char *path, void *ud);

enum {
    TRANSIENT_CLEAN = 0,
    TRANSIENT_UNMOUNT_FAILED = -1,
    TRANSIENT_MOUNTPOINT_FAILED = -2,
    TRANSIENT_PARENT_FAILED = -3,
    TRANSIENT_FILE_FAILED = -4,
};

/* The mountpoint belongs to one staging reader and must be empty after its
 * filesystem is detached. The parent is shared, so ENOTEMPTY is expected. */
static inline int transient_mount_cleanup(const char *mountpoint,
                                          const char *parent,
                                          int *mounted,
                                          transient_path_fn unmount_path,
                                          transient_path_fn remove_dir,
                                          void *ud)
{
    if (*mounted) {
        if (unmount_path(mountpoint, ud) != 0) return TRANSIENT_UNMOUNT_FAILED;
        *mounted = 0;
    }
    if (remove_dir(mountpoint, ud) != 0 && errno != ENOENT)
        return TRANSIENT_MOUNTPOINT_FAILED;
    if (remove_dir(parent, ud) != 0 && errno != ENOENT && errno != ENOTEMPTY)
        return TRANSIENT_PARENT_FAILED;
    return TRANSIENT_CLEAN;
}

/* saved.bin can contain the machine's startup files. Once its checked disk
 * copy exists, neither it nor any unknown staging scratch may cross in /run. */
static inline int transient_scratch_cleanup(const char *file, const char *dir,
                                            transient_path_fn remove_file,
                                            transient_path_fn remove_dir,
                                            void *ud)
{
    if (remove_file(file, ud) != 0 && errno != ENOENT)
        return TRANSIENT_FILE_FAILED;
    if (remove_dir(dir, ud) != 0 && errno != ENOENT)
        return TRANSIENT_PARENT_FAILED;
    return TRANSIENT_CLEAN;
}

#endif
