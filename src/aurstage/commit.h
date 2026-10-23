/* commit.h — phase 8: the partition table, committed so that a reader
 * at any instant finds a whole table.
 *
 * Rule 4 of docs/AURBRIDGE.md: "all data movement happens with the old
 * layout still in force; the new partition table is committed in one
 * step". The property that matters is not that one sector decides
 * everything -- it is that AT NO INSTANT does a reader find no table, a
 * damaged one, or a mixture, and that the layout only ever moves from
 * the old one to the new one.
 *
 * THE ORDER, AND THE TWO ORDERS THAT WERE WRONG
 *   1. the NEW backup (entry array, then header) at the last block of
 *      the disk, flushed and read back. The primary is untouched, valid
 *      and old, so every reader still uses it.
 *   2. the primary entry array. The primary header's CRC covers it, so
 *      from the first sector that lands the primary is rejected and
 *      readers fall back to the backup -- the new table, complete.
 *   3. LBA 1, the primary header. One sector. The primary agrees with
 *      the backup again.
 *   4. everything read back.
 * The first design (backup first, then primary header) was found by
 * committest to change the layout at the array write rather than at
 * LBA 1, and the second (primary array first, backup last) fixed that by
 * making the fallback the OLD backup -- which nothing validated. A disk
 * whose old backup was missing, stale or not at the end (cloned,
 * resized, a vendor tool) had no readable table between steps 1 and 2 of
 * that order, and a power cut there starts nothing at all. This order
 * relies on no table it did not write itself. It costs the claim that
 * the machine's layout changes at exactly one sector: it changes at the
 * first primary-array sector, or at LBA 1, whichever a reader sees first,
 * and either way to a table that is complete and checked.
 *
 * LBA 0 IS NEVER WRITTEN. The protective MBR describes the whole disk
 * with one entry, and the disk has not changed size, so there is
 * nothing in it to update. Rewriting it would be a second write in the
 * commit window for no reason, and it is the sector a machine that
 * will not boot at all is usually missing.
 */
#ifndef AUROS_COMMIT_H
#define AUROS_COMMIT_H

#include <stddef.h>
#include "gpt.h"
#include "plan.h"
#include "wr.h"

/* Build the new table from the old one and the layout: shrink the
 * Windows entry, add the AurOS root and the recovery partition.
 * Refuses on anything that does not fit or overlaps. */
int commit_build(const gpt_table *old, const stage_layout *L,
                 gpt_table *out, char *why, size_t n);

/* Write it, in the order above, and read every byte back.
 *
 * `t` must have WR_GPT_BACKUP and WR_GPT_PRIMARY armed. The primary
 * window deliberately includes LBA 1, and this is the only function
 * that writes it. */
/* `after` is called once after each flush, with the step number
 * (1 = the new backup is down and checked, the old table still in
 * force; 2 = the primary array is down, so the new table is in force
 * through the backup; 3 = LBA 1 has landed). The record uses it to write
 * down which step it reached, and the tests use it to stop the world
 * and ask what a reader would see at that instant -- which is the
 * only way to check the claim the ordering makes. */
int commit_table(wr_target *t, const gpt_table *nw,
                 void (*after)(int step, void *ud), void *ud,
                 char *why, size_t n);

/* After the commit: make the kernel notice, then check and grow the
 * root filesystem into the partition it now has.
 *
 * e2fsck RUNS FIRST, AND NOT AS A PRECAUTION. resize2fs refuses with
 * "Please run 'e2fsck -f' first" whenever s_lastcheck < s_mtime, and
 * that is true of every image build/mkimage produces -- it makes the
 * filesystem and then mounts it to fill it. Without this the root
 * never grows, silently, on every single install, and the user who
 * gave AurOS two hundred gigabytes gets seven. */
int commit_settle(const char *disk_dev, const char *root_dev,
                  char *why, size_t n);

#endif
