/* stagelog.c -- what the staging environment said, left where someone can read it.
 *
 * THE THIRD WRITER, with its own gate (build/staging). Everything this program says goes
 * to the screen and the serial console, and then the computer restarts by itself: a person
 * who was not looking, or a test harness that was not recording, is left with a verdict
 * word and no reason. This keeps the words, and at the end writes them to ONE file on the
 * machine's EFI partition, \EFI\AurOS\stage.log, beside the files AurBridge already put
 * there -- so that Windows, AurBridge on its next run, or a person with the disk in another
 * computer, can read why.
 *
 * It is allowed to write exactly this and nothing else, and the build refuses the image if
 * it ever opens anything read-write, names a device, or stops naming that file. It writes
 * only if \EFI\AurOS is already there: a restore that has put the saved EFI partition back
 * has removed AurOS from it, and a log written then would be AurOS leaving something behind.
 * It is a FAT file system mounted by the kernel, not a disk write of this program's own.
 *
 * Off with aurstage.nolog on the kernel line, which is what the tests that assert a refusal
 * leaves the disk byte for byte as it was pass; nothing a person does sets it. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aurstage.h"
#include "transient-mount.h"

#define ESP_MP   "/run/aurstage/esp"
#define LOG_FILE ESP_MP "/EFI/AurOS/stage.log"

static int g_mounted;

static int unmount_path(const char *path, void *ud)
{
    (void)ud;
    return umount2(path, 0);
}

static int remove_dir(const char *path, void *ud)
{
    (void)ud;
    return rmdir(path);
}

int stagelog_cleanup(char *why, size_t n)
{
    int rc = transient_mount_cleanup(ESP_MP, "/run/aurstage", &g_mounted,
                                     unmount_path, remove_dir, NULL);
    if (rc == TRANSIENT_CLEAN) return 0;
    if (rc == TRANSIENT_UNMOUNT_FAILED)
        snprintf(why, n, "the EFI log partition could not be let go of (%s)",
                 strerror(errno));
    else if (rc == TRANSIENT_MOUNTPOINT_FAILED)
        snprintf(why, n, "the EFI log mountpoint could not be removed (%s)",
                 strerror(errno));
    else
        snprintf(why, n, "the staging scratch directory could not be removed (%s)",
                 strerror(errno));
    return -1;
}

/* `esp` names an EFI System Partition's device node. Returns 1 if the log was written. */
int stagelog_write(const char *esp, const char *text, size_t n)
{
    char why[160];
    if (stagelog_cleanup(why, sizeof why) != 0) return 0;
    mkdir("/run/aurstage", 0700);
    mkdir(ESP_MP, 0700);
    const unsigned long keep = MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME;
    if (mount(esp, ESP_MP, "vfat", MS_RDONLY | keep, "") != 0) return 0;
    g_mounted = 1;
    int wrote = 0;
    struct stat st;
    if (stat(ESP_MP "/EFI/AurOS", &st) == 0 && S_ISDIR(st.st_mode) &&
        mount(esp, ESP_MP, "vfat", MS_REMOUNT | keep, "") == 0) {
        int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) {
            size_t at = 0;
            while (at < n) {
                ssize_t k = write(fd, text + at, n - at);
                if (k <= 0) break;
                at += (size_t)k;
            }
            wrote = at == n && fsync(fd) == 0;
            close(fd);
        }
    }
    sync();
    return stagelog_cleanup(why, sizeof why) == 0 && wrote;
}
