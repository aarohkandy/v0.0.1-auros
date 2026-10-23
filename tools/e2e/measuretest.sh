#!/bin/sh
# measuretest -- the staging's "how small can Windows get" survives a real
# volume's ntfsresize output.
#
# On a real Windows volume `ntfsresize --info` prints about 1,660 progress
# lines (~41 KB) before the line the whole call exists to get ("You might
# resize at ..."). shrink.c's capture() kept the head of the output and a
# ring for the tail, and stitched the tail on only if the head had room:
# on any overflow it had none, so every real PC was refused with
# "could be read but not measured" (verdict cannot-measure; e2e runs
# 39931612628 and 39959768382). The synthetic 2 GB test volumes print few
# progress lines, which is why no test noticed.
#
# This links the real shrink.c against a fake ntfsresize that prints N
# progress lines, for N from none to far more than a real volume.
#
#   sh tools/e2e/measuretest.sh
set -u
cd "$(dirname "$0")/../.."
command -v cc >/dev/null 2>&1 || { echo "measuretest: no C compiler, skipped"; exit 0; }
TMP=$(mktemp -d "${TMPDIR:-/tmp}/measuretest.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/ntfsresize" <<'EOS'
#!/bin/sh
echo "ntfsresize v2022.10.3 (libntfs-3g)"
echo "Current volume size: 136180658688 bytes (136181 MB)"
echo "Checking filesystem consistency ..."
i=0; while [ "$i" -lt "${PROGRESS_LINES:-0}" ]; do echo "  $i.06 percent completed"; i=$((i+1)); done
echo "Accounting clusters ..."
echo "Space in use       : 17912 MB (13.2%)"
echo "Collecting resizing constraints ..."
echo "You might resize at 17911664640 bytes or 17912 MB (freeing 118269 MB)."
EOS
chmod +x "$TMP/ntfsresize"

cat > "$TMP/t.c" <<'EOC'
#include <stdio.h>
#include "shrink.h"
int main(void)
{
    shrink_plan sp;
    shrink_ask("/dev/null", &sp);
    printf("%d %llu %llu\n", sp.ok, (unsigned long long)sp.current_bytes,
           (unsigned long long)sp.smallest_bytes);
    return 0;
}
EOC
cc -O2 -std=gnu11 -Wall -I src/aurstage -DNTFSRESIZE_PATH="\"$TMP/ntfsresize\"" -o "$TMP/t" "$TMP/t.c" \
   src/aurstage/shrink.c src/aurstage/plan.c src/aurstage/gpt.c \
   src/aurstage/ntfs.c src/aurstage/sha256.c src/aurstage/fde.c 2>"$TMP/cc.log" \
   || { echo "measuretest: did not build"; sed -n 1,20p "$TMP/cc.log"; exit 2; }

fail=0
for n in 0 20 200 700 1660 5000; do
    got=$(PROGRESS_LINES=$n "$TMP/t")
    if [ "$got" = "1 136180658688 17911664640" ]; then
        printf '    %-62s ok\n' "ntfsresize prints $n progress lines: measured"
    else
        printf '    %-62s FAIL (%s)\n' "ntfsresize prints $n progress lines: measured" "$got"
        fail=$((fail+1))
    fi
done
[ "$fail" -eq 0 ] || { echo "$fail wrong."; exit 1; }
echo "measuretest: a real volume's output is measured."
