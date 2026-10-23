/* fuzz_gpt.c -- the GPT reader and writer against a disk someone else wrote.
 *
 * src/aurstage/gpt.c (stream A2's file; S3 only fuzzes it) reads her
 * disk's partition table -- primary, then backup -- on Windows (the
 * planner) and in the staging environment, and writes the new one. The
 * table was last written by Windows, a recovery tool, a vendor imaging
 * tool or a disk that is failing: every field is untrusted. This harness
 * makes the fuzzer's bytes the whole disk, reads it through
 * gpt_read_with() (the same entry the planner uses), runs every query
 * the planner asks of the table, then does what commit.c does with it:
 * serialises it, and checks that what we would write is a header we
 * ourselves would read back -- a table we cannot re-read after the
 * commit is a machine that does not start.
 *
 * Input: [sector byte][disk image]. Bit 0 of the sector byte picks a
 * 512- or 4096-byte logical sector.
 */
#include "../../src/aurstage/gpt.c"

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

    /* What commit_table() writes, read back the way gpt_read() would.
     * Only for a table read from the primary: rescue_capture() refuses a
     * backup-only disk before any commit can happen (rescue.c), so a
     * backup's entry_lba never becomes a primary header's. */
    if (t.from_backup) return 0;
    size_t ab = gpt_array_bytes(&t);
    uint8_t *hdr = malloc(t.sector), *arr = malloc(ab ? ab : 1);
    if (!hdr || !arr) abort();
    for (int primary = 1; primary >= 0; primary--) {
        gpt_serialize(&t, primary, hdr, arr);
        static gpt_table u;
        memset(&u, 0, sizeof u);
        if (parse_header(hdr, t.sector, &u) != 0) abort();
        if (u.n_entries != t.n_entries || u.entry_size != t.entry_size ||
            rd32(hdr + 88) != gpt_crc32(arr, ab))
            abort();
    }
    free(hdr); free(arr);
    return 0;
}
