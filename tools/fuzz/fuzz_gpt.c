/* fuzz_gpt.c -- the GPT reader and writer against a disk someone else wrote.
 *
 * src/aurstage/gpt.c (stream A2's file; S3 only fuzzes it) reads her
 * disk's partition table -- primary, then backup -- on Windows (the
 * planner) and in the staging environment, and writes the new one. The
 * table was last written by Windows, a recovery tool, a vendor imaging
 * tool or a disk that is failing: every field is untrusted. This harness
 * makes the fuzzer's bytes the whole disk, reads it through
 * gpt_read_with() (the same entry the planner uses), runs every query
 * the planner asks of the table, and then plays commit_table() (commit.c)
 * on a copy of the disk, writing exactly what it writes where it writes
 * it. Two promises are checked:
 *
 *   1. NOTHING IT WRITES LANDS IN A PARTITION. The four writes (primary
 *      array and header, backup array and header) go where the table's
 *      own header fields say; if those point into her Windows partition,
 *      that is her data overwritten by a partition table.
 *   2. WHAT IT WRITES, gpt_read() READS BACK: valid, from the primary,
 *      the same entries. A table we cannot re-read after the commit is a
 *      machine that does not start.
 *
 * Writes that commit.c's wr_arm() gate would refuse (off the disk, or the
 * primary and backup windows overlapping) are a refusal, not a finding.
 * A table read from the backup is not committed at all: rescue_capture()
 * refuses one first (rescue.c).
 *
 * Input: [sector byte][disk image]. Bit 0 of the sector byte picks a
 * 512- or 4096-byte logical sector.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/aurstage/gpt.h"

typedef struct { const uint8_t *p; size_t n; } disk;

static int disk_read(void *ud, uint64_t off, void *buf, size_t n)
{
    const disk *d = ud;
    if (off > d->n || n > d->n - off) return -1;
    memcpy(buf, d->p + off, n);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > (2u << 20)) return 0;
    uint32_t ss = (data[0] & 1) ? 4096 : 512;
    disk d = { data + 1, size - 1 };

    static gpt_table t;
    if (gpt_read_with(disk_read, &d, ss, d.n, &t) != 0) return 0;
    if (!t.valid || t.n_entries == 0 || t.n_entries > GPT_MAX_ENT) abort();

    /* Every question plan.c / commit.c asks of a table. */
    (void)gpt_find_start(&t, t.ent[0].first);
    (void)gpt_gap_end(&t, t.first_usable);
    (void)gpt_overlaps(&t, t.first_usable, t.last_usable, -1);
    for (uint32_t i = 0; i < t.n_entries; i++)
        if (gpt_used(&t.ent[i]) && t.ent[i].last > t.ent[i].first) {
            (void)gpt_resize_entry(&t, (int)i, t.ent[i].last - 1);
            break;
        }
    char why[256];
    uint64_t s = gpt_gap_end(&t, 0);
    (void)gpt_add(&t, GPT_TYPE_LINUX_ROOT, "AUROS-ROOT-WITH-A-NAME-LONGER-THAN-36-CHARACTERS",
                  t.first_usable, s ? s - 1 : 0, NULL, why, sizeof why);

    if (t.from_backup) return 0;

    /* commit_table(), on a copy. */
    uint64_t S = t.sector, ab = gpt_array_bytes(&t);
    uint64_t alt = t.disk_sectors - 1, barr = alt - (ab + S - 1) / S;
    struct { uint64_t off, len; } w[4] = {
        { t.entry_lba * S, ab }, { S, S }, { barr * S, ab }, { alt * S, S },
    };
    uint64_t plo = S, phi = t.entry_lba * S + ab, blo = barr * S, bhi = (alt + 1) * S;
    if (t.entry_lba > UINT64_MAX / S - 1 || barr > alt || phi <= plo ||
        phi > d.n || bhi > d.n || (plo < bhi && blo < phi))
        return 0;                                   /* wr_arm() says no */

    for (int k = 0; k < 4; k++)
        for (uint32_t i = 0; i < t.n_entries; i++) {
            const gpt_entry *e = &t.ent[i];
            if (!gpt_used(e) || e->last < e->first) continue;
            uint64_t a = e->first * S, b = (e->last + 1) * S;
            if (e->first > UINT64_MAX / S - 1 || e->last > UINT64_MAX / S - 1) continue;
            if (w[k].off < b && a < w[k].off + w[k].len) {
                fprintf(stderr, "commit would write %llu bytes at %llu, inside partition %u [%llu, %llu)\n",
                        (unsigned long long)w[k].len, (unsigned long long)w[k].off, i + 1,
                        (unsigned long long)a, (unsigned long long)b);
                abort();
            }
        }

    uint8_t *copy = malloc(d.n), *hdr = malloc(S), *arr = malloc(ab ? ab : 1);
    if (!copy || !hdr || !arr) abort();
    memcpy(copy, d.p, d.n);
    gpt_serialize(&t, 1, hdr, arr);
    memcpy(copy + w[0].off, arr, ab);
    memcpy(copy + w[1].off, hdr, S);
    gpt_serialize(&t, 0, hdr, arr);
    memcpy(copy + w[2].off, arr, ab);
    memcpy(copy + w[3].off, hdr, S);

    static gpt_table u;
    disk c = { copy, d.n };
    if (gpt_read_with(disk_read, &c, t.sector, d.n, &u) != 0 || !u.valid || u.from_backup ||
        u.n_entries != t.n_entries || memcmp(u.ent, t.ent, sizeof t.ent[0] * t.n_entries)) {
        fprintf(stderr, "the table commit_table() would write does not read back\n");
        abort();
    }
    free(copy); free(hdr); free(arr);
    return 0;
}
