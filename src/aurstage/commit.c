/* commit.c — see commit.h. Writes only through wr.c. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>

#include "commit.h"
#include "fault.h"
#include "rescue.h"

int commit_build(const gpt_table *old, const stage_layout *L,
                 gpt_table *out, char *why, size_t n)
{
    /* A TABLE READ FROM THE BACKUP IS NEVER COMMITTED. Its array is at
     * the end of the disk, where the primary's must not be, and a
     * primary that was torn is a disk something else is wrong with
     * (rescue_capture refuses it first; this is the second lock). */
    if (old->from_backup) {
        snprintf(why, n,
                 "this disk's main partition table is damaged, and AurOS will "
                 "not change a disk it is reading from its spare copy");
        return -1;
    }
    *out = *old;
    out->from_backup = 0;

    /* THE FAST PATH'S WINDOWS IS ALREADY ITS NEW SIZE (Windows shrank it
     * itself, plan_compute_prepared), so there is nothing to resize --
     * and gpt_resize_entry() rightly refuses a "shrink" to the same end. */
    if (L->win_last_new != old->ent[L->win_idx].last &&
        gpt_resize_entry(out, L->win_idx, L->win_last_new) != 0) {
        snprintf(why, n,
                 "the Windows partition could not be made smaller in the "
                 "partition table");
        return -1;
    }
    /* The order they are added in does not matter to the firmware --
     * entries are not required to be sorted -- but adding the root
     * first keeps the numbering stable across the two profiles this
     * has to work for. */
    if (gpt_add(out, GPT_TYPE_LINUX_ROOT, "AUROS-ROOT",
                L->root_first, L->root_last, NULL, why, n) < 0)
        return -1;
    /* The saved copy of this machine's Windows startup. A RAW extent
     * with our own type GUID and no filesystem in it -- see the note
     * on rsc_first in plan.h for why it is not a file inside the
     * partition below. */
    if (gpt_add(out, RESCUE_TYPE_GUID, "AUROS-SAVED",
                L->rsc_first, L->rsc_last, NULL, why, n) < 0)
        return -1;
    /* WHAT STARTS THE COMPUTER, and it is an EFI System partition of
     * our own rather than a directory inside the machine's.
     *
     * loader.h has the argument at length. The short form is R12: the
     * machine's EFI partition holds vendor boot files and firmware
     * capsules as well as \EFI\Microsoft\Boot, and is never mounted
     * by this program. UEFI launches whatever a Boot#### entry names
     * and does not care which EFI System partition that is, so AurOS
     * gets one it owns outright -- and Windows' own remains a
     * partition this installer has only ever read.
     *
     * It was called AUROS-RECOVERY while it was going to hold a rescue
     * environment and nothing else did. It holds the bootloader, so it
     * says so: somebody reading this disk in a partition manager
     * should be able to tell which partition starts the computer. */
    if (gpt_add(out, GPT_TYPE_ESP, "AUROS-BOOT",
                L->rec_first, L->rec_last, NULL, why, n) < 0)
        return -1;
    return 0;
}

int commit_table(wr_target *t, const gpt_table *nw,
                 void (*after)(int step, void *ud), void *ud,
                 char *why, size_t n)
{
    size_t ab = gpt_array_bytes(nw);
    uint32_t ss = nw->sector;
    uint8_t *phdr = malloc(ss), *bhdr = malloc(ss);
    uint8_t *arr  = malloc(ab);
    int rc = -1;
    if (!phdr || !bhdr || !arr) {
        snprintf(why, n, "this computer ran out of memory");
        goto out;
    }

    gpt_serialize(nw, 1, phdr, arr);
    gpt_serialize(nw, 0, bhdr, arr);      /* the array is the same     */

    uint64_t alt  = nw->disk_sectors - 1;
    uint64_t barr = alt - (ab + ss - 1) / ss;

    /* 1. THE NEW BACKUP, FIRST, at the last block of the disk, array
     *    then header (so a torn backup has no valid header and is simply
     *    not believed), flushed, and READ BACK before anything primary is
     *    touched. This used to be last, and the step before it
     *    invalidated the primary and relied on whatever backup the disk
     *    already had -- one nothing had ever looked at. A disk whose old
     *    backup was missing, stale or never at the end (cloned, resized,
     *    a vendor tool) had no readable partition table at all between
     *    the primary array and LBA 1, and a power cut there is a
     *    computer that starts nothing (docs/safety, finding on commit.c).
     *    Now the primary is still the old, valid table: readers use it
     *    and the new backup just sits at the end. */
    if (wr_bytes(t, WR_GPT_BACKUP, barr * ss, arr, ab, why, n) != 0) goto out;
    if (wr_bytes(t, WR_GPT_BACKUP, alt * ss, bhdr, ss, why, n) != 0) goto out;
    if (wr_flush(t) != 0) {
        snprintf(why, n, "this computer's disk would not finish writing");
        goto out;
    }
    {
        uint64_t bad1 = 0;
        if (wr_check(t, barr * ss, arr, ab, &bad1) != 0 ||
            wr_check(t, alt * ss, bhdr, ss, &bad1) != 0) {
            snprintf(why, n,
                     "the spare copy of the new partition table did not read "
                     "back the same as it was written. Nothing in the "
                     "partition table in use has been changed.");
            goto out;
        }
    }
    if (after) after(1, ud);

    /* 2. THE PRIMARY ENTRY ARRAY. Its header's CRC covers it, so the
     *    moment any of it lands the primary is rejected and every reader
     *    falls back to the backup -- which is now the NEW table, whole
     *    and checked. From here the disk reads as the new layout,
     *    whether this write finishes or tears. There is no instant at
     *    which a reader finds neither table or a mixture: it reads the
     *    old one (primary valid) or the new one (backup, or primary
     *    again), and it only ever moves old -> new. */
    if (wr_bytes(t, WR_GPT_PRIMARY, nw->entry_lba * ss, arr, ab, why, n) != 0)
        goto out;
    if (wr_flush(t) != 0) {
        snprintf(why, n, "this computer's disk would not finish writing");
        goto out;
    }
    if (after) after(2, ud);

    /* 3. LBA 1: the primary header, one sector, and the primary agrees
     *    with the backup again. */
    if (wr_bytes(t, WR_GPT_PRIMARY, 1ull * ss, phdr, ss, why, n) != 0) goto out;
    if (wr_flush(t) != 0) {
        snprintf(why, n, "this computer's disk would not finish writing");
        goto out;
    }
    if (after) after(3, ud);

    /* 4. Read it all back. A commit that is only in a write cache is
     *    not a commit, and the drives this product is for are old
     *    enough to have one. */
    uint64_t bad = 0;
    if (wr_check(t, 1ull * ss, phdr, ss, &bad) != 0 ||
        wr_check(t, nw->entry_lba * ss, arr, ab, &bad) != 0 ||
        wr_check(t, alt * ss, bhdr, ss, &bad) != 0 ||
        wr_check(t, barr * ss, arr, ab, &bad) != 0) {
        snprintf(why, n,
                 "the new partition table did not read back the same as it "
                 "was written.");
        goto out;
    }
    rc = 0;
out:
    free(phdr); free(bhdr); free(arr);
    return rc;
}

/* A fixed argument vector, as everywhere a program is run from here.
 * Returns the exit status, or -1. */
static int run_fixed(const char *const argv[], char *tail, size_t tn)
{
    int p[2];
    if (tail && tn) tail[0] = 0;
    if (pipe(p) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1); dup2(p[1], 2);
        if (p[1] > 2) close(p[1]);
        setenv("LC_ALL", "C", 1);
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(p[1]);
    size_t got = 0;
    char buf[1024];
    for (;;) {
        ssize_t k = read(p[0], buf, sizeof buf);
        if (k <= 0) break;
        if (tail && tn) {
            for (ssize_t i = 0; i < k; i++) {
                if (got + 1 < tn) tail[got++] = buf[i];
            }
            tail[got] = 0;
        }
    }
    close(p[0]);
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int commit_settle(const char *disk_dev, const char *root_dev,
                  char *why, size_t n)
{
    /* 1. Tell the kernel the table changed. BLKRRPART rather than
     *    shelling out to partx: it is one ioctl, it is what partx
     *    itself calls, and it keeps another program out of the
     *    initramfs. */
    int fd = open(disk_dev, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ioctl(fd, _IO(0x12, 95));          /* BLKRRPART */
        close(fd);
    }
    /* The node for the new partition has to appear before anything can
     * be done to it. devtmpfs creates it, and there is no udev here to
     * wait on, so this waits on the file. */
    for (int i = 0; i < 100; i++) {
        if (access(root_dev, F_OK) == 0) break;
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    if (access(root_dev, F_OK) != 0) {
        snprintf(why, n,
                 "this computer did not notice its new layout. AurOS is on "
                 "the disk and Windows still starts; please restart the "
                 "computer.");
        return -1;
    }

    /* 2. e2fsck FIRST. Not a precaution: resize2fs refuses outright
     *    with "Please run 'e2fsck -f' first" whenever s_lastcheck is
     *    older than s_mtime, which is true of every image mkimage
     *    produces, because it makes the filesystem and then mounts it
     *    to fill it. Without this the root never grows -- silently, on
     *    every install. -p is "preen": fix what is safe, refuse what
     *    is not, never ask a question there is nobody to answer. */
    char tail[1024];
    const char *fsck[] = { "/sbin/e2fsck", "-fp", root_dev, NULL };
    int rc = run_fixed(fsck, tail, sizeof tail);
    /* 0 = clean, 1 = fixed something. Anything else is a filesystem we
     * should not be growing. */
    if (rc == 127) {
        /* THE PROGRAM IS NOT THERE. Not a filesystem problem, and
         * saying it is sends a support engineer looking at the disk
         * instead of at our build. This exact thing happened: e2fsck
         * was not in the staging image at all, and the install
         * reported that the filesystem it had just written and
         * hash-verified had failed its own check. */
        snprintf(why, n,
                 "AurOS could not run the tool that checks its own "
                 "filesystem. This is a fault in the AurOS memory stick.");
        return -1;
    }
    if (rc != 0 && rc != 1) {
        snprintf(why, n,
                 "the new AurOS filesystem did not pass its own check, so it "
                 "was left at the size it was written.");
        return -1;
    }

    /* 3. Grow it into the partition. */
    const char *grow[] = { "/sbin/resize2fs", root_dev, NULL };
    /* A power cut INSIDE resize2fs (docs/safety S-11): a fault build can ask to stop here, 80 ms
     * into it. Nothing in a release build. */
    fault_maybe_soon("settle-mid", 80);
    rc = run_fixed(grow, tail, sizeof tail);
    if (rc != 0) {
        /* NOT FATAL, and this is a judgement worth stating: AurOS is
         * written, verified and bootable at the size it came as. A
         * root that did not grow is a smaller disk than the person
         * chose, which is a disappointment; refusing here would
         * instead leave them with a committed partition table and no
         * system, which is a catastrophe. */
        snprintf(why, n,
                 "AurOS was installed but could not be grown to fill the "
                 "space it was given.");
        return 1;
    }
    why[0] = 0;
    return 0;
}
