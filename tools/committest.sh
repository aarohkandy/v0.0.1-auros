#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  committest — phase 8, and the claim the whole ordering makes
#
#  Rule 4: "one atomic commit point per destructive phase. All data
#  movement happens with the old layout still in force; the new
#  partition table is a single sector write."
#
#  That claim is testable and this file tests it. The commit is
#  stopped after each flush and the disk is read back the way a
#  firmware or a kernel would read it. Until LBA 1 lands the answer
#  must be THE OLD LAYOUT, every time. After it lands, the new one.
#
#  A snapshot is taken at each step rather than reasoning about the
#  order, because reasoning about write ordering is how everybody who
#  has ever got this wrong got it wrong.
#
#    sh tools/committest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-56s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-56s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

command -v sgdisk >/dev/null 2>&1 || { echo "need sgdisk"; exit 2; }
TMP=$(mktemp -d "${TMPDIR:-/tmp}/committest.XXXXXX")
# A kill (timeout, ^C) must not leak the images in $TMP: the EXIT trap alone
# does not run on a signal in dash, and a leaked 3 GB disk image per run
# once filled homebase. INT and TERM exit, which runs the EXIT trap, and
# the background QEMU goes with it.
_a2_clean() { kill $(jobs -p) 2>/dev/null; [ -n "${KEEP:-}" ] || rm -rf "$TMP"; }
trap _a2_clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "Is there ever an instant at which the disk has no whole table, and"
echo "does the layout move from old to new exactly once?"
echo

cat > "$TMP/c.c" <<'EOC'
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "commit.h"

static const char *g_img;
static int g_stop;          /* snapshot after this step and stop     */

/* Copy the whole disk the moment a step finishes, so it can be read
 * back exactly as a reader would have found it at that instant. */
static void snap(int step, void *ud)
{
    (void)ud;
    char dst[512];
    snprintf(dst, sizeof dst, "%s.step%d", g_img, step);
    int a = open(g_img, O_RDONLY), b = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (a >= 0 && b >= 0) {
        /* SPARSE: the disk is 3 GiB of which a few MiB are not zero, and
         * a full copy per step once filled the build server's disk. */
        static char buf[1 << 16], zero[1 << 16];
        ssize_t k;
        while ((k = read(a, buf, sizeof buf)) > 0) {
            if (memcmp(buf, zero, (size_t)k) == 0) { lseek(b, k, SEEK_CUR); continue; }
            ssize_t w = write(b, buf, (size_t)k); (void)w;
        }
        off_t end = lseek(a, 0, SEEK_END);
        if (ftruncate(b, end) != 0) { /* the copy is merely longer */ }
    }
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    if (g_stop && step >= g_stop) _exit(7);   /* the power goes here */
}

static uint64_t fsize(const char *p)
{ struct stat st; return stat(p, &st) == 0 ? (uint64_t)st.st_size : 0; }

int main(int argc, char **argv)
{
    /* commit <img> <win_first_lba> <win_new_bytes> <root_src> <rec> <min> [stop] */
    if (argc < 7) return 2;
    g_img = argv[1];
    g_stop = argc > 7 ? atoi(argv[7]) : 0;

    int fd = open(g_img, O_RDONLY);
    gpt_table old;
    if (fd < 0 || gpt_read(fd, 512, fsize(g_img), &old) != 0) {
        puts("read=failed"); return 2;
    }
    close(fd);

    int wi = gpt_find_start(&old, strtoull(argv[2], NULL, 10));
    stage_layout L; char why[240] = {0};
    if (plan_compute(&old, wi, strtoull(argv[3], NULL, 10),
                     strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10),
                     128ull * 1024 * 1024,
                     strtoull(argv[6], NULL, 10), &L, why, sizeof why) != 0) {
        printf("plan=failed\n"); printf("why=%s\n", why); return 2;
    }
    gpt_table nw;
    if (commit_build(&old, &L, &nw, why, sizeof why) != 0) {
        printf("build=failed\n"); printf("why=%s\n", why); return 2;
    }
    printf("build=ok\n");

    wr_target t;
    if (wr_open(&t, g_img, why, sizeof why) != 0) {
        printf("open=failed\n"); return 2;
    }
    uint32_t ss = nw.sector;
    size_t ab = gpt_array_bytes(&nw);
    uint64_t alt = nw.disk_sectors - 1;
    uint64_t barr = alt - (ab + ss - 1) / ss;
    if (wr_arm(&t, WR_GPT_BACKUP, barr * ss, (alt + 1) * ss, why, sizeof why) != 0 ||
        wr_arm(&t, WR_GPT_PRIMARY, 1ull * ss,
               nw.entry_lba * ss + ab, why, sizeof why) != 0) {
        printf("arm=failed\n"); printf("why=%s\n", why); return 2;
    }
    int r = commit_table(&t, &nw, snap, NULL, why, sizeof why);
    printf("commit=%d\n", r);
    printf("why=%s\n", why);
    wr_close(&t);
    return r ? 1 : 0;
}
EOC
gcc -O1 -g -std=gnu11 -Wall -Wextra -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I src/aurstage -o "$TMP/c" "$TMP/c.c" \
    src/aurstage/commit.c src/aurstage/gpt.c src/aurstage/plan.c \
    src/aurstage/wr.c src/aurstage/boot.c src/aurstage/stagelog.c src/aurstage/disks.c \
    src/aurstage/sha256.c src/aurstage/ntfs.c src/aurstage/fde.c \
    src/aurstage/rescue.c src/aurstage/shrink.c src/aurstage/fault.c \
    || { echo "  did not build"; exit 2; }

# The OEM shape again: ESP, MSR, Windows, and a recovery partition at
# the very end, with a gap opened in the middle by the shrink.
mkdisk() {
    D="$1"; rm -f "$D"; truncate -s 3G "$D"
    sgdisk --zap-all "$D" >/dev/null 2>&1
    sgdisk -n 1:2048:+260M -t 1:ef00 -c 1:"EFI"     "$D" >/dev/null 2>&1
    sgdisk -n 2:0:+128M    -t 2:0c01 -c 2:"MSR"     "$D" >/dev/null 2>&1
    sgdisk -n 3:0:+1G      -t 3:0700 -c 3:"Windows" "$D" >/dev/null 2>&1
    sgdisk -n 4:5500000:0  -t 4:2700 -c 4:"WinRE"   "$D" >/dev/null 2>&1
}
parts() { sgdisk -p "$1" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 |
          awk 'NF{print $1":"$2":"$3}'; }
G() { sed -n "s/^$1=//p"; }

D="$TMP/d.img"; mkdisk "$D"
WS=$(sgdisk -i 3 "$D" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
BEFORE=$(parts "$D")
echo "  a disk with 4 partitions and a gap after Windows"

echo
echo "  building the new table"
OUT=$("$TMP/c" "$D" "$WS" 536870912 268435456 209715200 200000000 2>/dev/null)
[ "$(printf '%s\n' "$OUT" | G build)" = "ok" ] && ok "the new table is built" \
    || bad "the new table is built" "$OUT"
[ "$(printf '%s\n' "$OUT" | G commit)" = "0" ] && ok "and written" \
    || bad "and written" "$(printf '%s\n' "$OUT" | G why)"

AFTER=$(parts "$D")
N=$(printf '%s\n' "$AFTER" | wc -l)
# Four before (ESP, MSR, Windows, OEM recovery) plus the three AurOS
# adds: the root, the saved copy of this machine's Windows startup, and
# the recovery partition.
[ "$N" = "7" ] && ok "the disk now has 7 partitions" || bad "the disk now has 7 partitions" "got $N"
# sgdisk is an independent reader: if it agrees, our CRCs are right.
if sgdisk -v "$D" 2>&1 | grep -q "No problems found"; then
    ok "and another tool agrees the table is valid"
else
    bad "and another tool agrees the table is valid" "$(sgdisk -v "$D" 2>&1 | head -3)"
fi
printf '%s\n' "$AFTER" | sed 's/^/      p/'

echo
echo "  and what a reader would have seen at each instant"
# Snapshots were taken after each flush. The layout must move from the
# old one to the new one exactly once, and every snapshot must be a disk
# a reader can read.
if [ -f "$D.step1" ]; then
    GOT=$(parts "$D.step1")
    [ "$GOT" = "$BEFORE" ] && ok "after step 1 (the new backup is down) it still reads as the old layout" \
        || bad "after step 1 it still reads as the old layout" \
               "the primary was touched before the backup was safe"
    V=$(sgdisk -v "$D.step1" 2>&1)
    printf '%s' "$V" | grep -q "Main partition table: OK" \
        && ok "...and its primary table is intact" \
        || bad "...and its primary table is intact" "$(printf '%s' "$V" | head -3)"
else
    bad "after step 1 it still reads as the old layout" "no snapshot"
fi
if [ -f "$D.step2" ]; then
    GOT=$(parts "$D.step2")
    [ "$GOT" != "$BEFORE" ] && ok "after step 2 (the primary array is down) it reads as the new layout" \
                            || bad "after step 2 it reads as the new layout" "still the old layout"
    # fdisk (libfdisk) is a reader that validates both CRCs the way the
    # kernel and the firmware do, and says which table it fell back to.
    if command -v fdisk >/dev/null 2>&1; then
        V=$(fdisk -l "$D.step2" 2>&1)
        printf '%s' "$V" | grep -q "primary GPT table is corrupt, but the backup appears OK" \
            && ok "...through a backup table that is whole (a reader falls back to it)" \
            || bad "...through a backup table that is whole" "$(printf '%s' "$V" | head -3)"
        [ "$(printf '%s' "$V" | grep -c "^$D.step2p")" = 7 ] \
            && ok "...and that table lists all 7 partitions" \
            || bad "...and that table lists all 7 partitions" "$(printf '%s' "$V" | grep -c "^$D.step2p")"
    fi
else
    bad "after step 2 it reads as the new layout" "no snapshot"
fi
if [ -f "$D.step3" ]; then
    if sgdisk -v "$D.step3" 2>&1 | grep -q "No problems found"; then
        ok "and after step 3 (LBA 1) both copies of the table agree"
    else
        bad "and after step 3 both copies of the table agree" \
            "$(sgdisk -v "$D.step3" 2>&1 | head -3)"
    fi
fi

echo
echo "  and a power cut after the new backup is down"
D2="$TMP/d2.img"; mkdisk "$D2"
B2=$(parts "$D2")
"$TMP/c" "$D2" "$WS" 536870912 268435456 209715200 200000000 1 >/dev/null 2>&1
A2=$(parts "$D2" 2>/dev/null)
[ "$A2" = "$B2" ] && ok "the machine still has exactly its old partitions" \
                  || bad "the machine still has exactly its old partitions" \
                         "before: $(printf '%s' "$B2" | tr '\n' ' ')" \
                         "after:  $(printf '%s' "$A2" | tr '\n' ' ')"
sgdisk -v "$D2" 2>&1 | grep -q "Main partition table: OK" \
    && ok "...read from a primary table that is whole" \
    || bad "...read from a primary table that is whole" "$(sgdisk -v "$D2" 2>&1 | head -3)"

echo
echo "  and a power cut at either instant, whatever the OLD backup was"
# THE CASE THE OLD ORDER FAILED. A disk whose original backup table is
# missing (cloned, resized, a vendor tool zeroed it) had, in the old
# order, no readable table at all between the primary array and LBA 1.
# Now the cut at step 1 leaves the old (valid) primary, and the cut at
# step 2 the new table through the backup written at step 1.
for kind in intact zeroed; do
    for stop in 1 2; do
        D3="$TMP/d3-$kind-$stop.img"; mkdisk "$D3"
        if [ "$kind" = zeroed ]; then
            SZ=$(stat -c%s "$D3")
            dd if=/dev/zero of="$D3" bs=512 seek=$((SZ/512 - 34)) count=34 conv=notrunc status=none
        fi
        "$TMP/c" "$D3" "$WS" 536870912 268435456 209715200 200000000 $stop >/dev/null 2>&1
        N3=$(parts "$D3" 2>/dev/null | wc -l)
        want=$([ "$stop" = 1 ] && echo 4 || echo 7)
        [ "$N3" = "$want" ] && ok "$kind old backup, cut at step $stop: $want partitions readable" \
                            || bad "$kind old backup, cut at step $stop: a readable table" "$N3 partitions read, wanted $want"
    done
done

# And on the disk that DID commit, Windows is smaller than it was.
WOLD=$(printf '%s\n' "$BEFORE" | sed -n '3p' | cut -d: -f3)
WNEW=$(printf '%s\n' "$AFTER"  | sed -n '3p' | cut -d: -f3)
if [ -n "$WOLD" ] && [ -n "$WNEW" ] && [ "$WNEW" -lt "$WOLD" ]; then
    ok "and on the committed disk Windows really is smaller"
else
    bad "and on the committed disk Windows really is smaller" \
        "was $WOLD, now $WNEW"
fi

echo
echo "  and a disk whose main table is damaged is not committed onto"
D4="$TMP/d4.img"; mkdisk "$D4"
dd if=/dev/zero of="$D4" bs=512 seek=1 count=1 conv=notrunc status=none
S4=$(cksum < "$D4")
OUT4=$("$TMP/c" "$D4" "$WS" 536870912 268435456 209715200 200000000 2>/dev/null)
[ "$(printf '%s\n' "$OUT4" | G build)" = "failed" ] && ok "building from the spare copy is refused" \
    || bad "building from the spare copy is refused" "$OUT4"
[ "$(cksum < "$D4")" = "$S4" ] && ok "...and the disk is byte-for-byte untouched" \
    || bad "...and the disk is byte-for-byte untouched"

echo
if [ "$fail" -gt 0 ]; then
    echo "$fail of $checked wrong."
    echo "This is the sector that decides where a stranger's partitions are."
    exit 1
fi
echo "$checked checks: at no instant is there no whole table, and the layout"
echo "moves from the old one to the new one exactly once."
exit 0
