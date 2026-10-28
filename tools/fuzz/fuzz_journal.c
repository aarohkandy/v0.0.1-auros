/* fuzz_journal.c -- the install journal, as the staging environment reads it.
 *
 * src/aurstage/journal.c (stream A2's file; S3 only fuzzes it) parses
 * the flat JSON record AurBridge wrote on the Windows side before the
 * restart. It sits on a FAT/NTFS volume any Windows program can write,
 * and it decides whether the staging environment touches the disk. The
 * fuzzer's bytes go through the real journal_read() (via a memfd, path
 * and all); whatever it accepts is then judged by journal_check()
 * against a machine whose one disk matches the record, so the verdict
 * code reads every field it was handed.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../../src/aurstage/journal.h"

/* journal_check() hashes the live partition table through this; disks.c
 * (which reads a device) is not under test. A fixed answer lets an
 * input that carries the same hash reach every check after it. */
#define DISK_HASH "8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4"
int stage_gpt_sha256(const stage_disk *d, char *hex, size_t n)
{
    (void)d;
    snprintf(hex, n, "%s", DISK_HASH);
    return 0;
}

static int  memfd = -1;
static char memfd_path[64];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 32768) return 0;
    if (memfd < 0) {
        memfd = memfd_create("fuzz-journal", MFD_CLOEXEC);
        if (memfd < 0) abort();
        snprintf(memfd_path, sizeof memfd_path, "/proc/self/fd/%d", memfd);
    }
    if (ftruncate(memfd, 0) < 0) abort();
    if (size && pwrite(memfd, data, size, 0) != (ssize_t)size) abort();

    static journal j;
    if (!journal_read(memfd_path, &j)) {
        if (j.present) abort();          /* refused, yet says it is usable */
        return 0;
    }
    if (!j.present || j.corrupt) abort();

    static stage_machine m;
    memset(&m, 0, sizeof m);
    m.n_disks = 1;
    stage_disk *d = &m.disk[0];
    snprintf(d->serial, sizeof d->serial, "%s", j.disk_serial);
    snprintf(d->model, sizeof d->model, "%s", j.disk_model);
    d->bytes = j.disk_bytes;
    d->logical_sector = (int)j.logical_sector;
    d->sector_known = 1;
    d->n_parts = 1;
    d->part[0].start_lba = j.win_start_lba;
    d->part[0].sectors = j.win_sectors;

    char why[512];
    journal_verdict v = journal_check(&j, &m, why, sizeof why);
    (void)journal_verdict_name(v);
    int how = 0;
    (void)journal_disk(&j, &m, &how);
    return 0;
}
