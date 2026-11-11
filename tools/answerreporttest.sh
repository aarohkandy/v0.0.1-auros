#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  answerreporttest.sh -- what answer.sh's "report:/dev/NAME" branch SAYS.
#
#    sh tools/answerreporttest.sh
#
#  rootfs/usr/lib/auros/answer.sh runs as root and turns the way a report ended
#  into the answer the desktop reads (src/aurshell/welcome.c shows its note).
#  Here every program it runs on the stick is a fake: ferry, blkid, mount,
#  umount, sync and the reporter (stick-report, proved by stickreporttest.sh).
#  Proved, for each way it can end:
#    - only a report that was written AND flushed AND let go of is result=ok
#      ("it is safe to take the stick out" is on that screen);
#    - a stick that will not mount, will not let go, will not flush, has no room
#      (the same why=noroom as the files-to-a-stick branch) or whose reporter
#      fails says so, and never says ok;
#    - a step that hangs (a dying stick) is stopped and said, within the limit:
#      this unit has no start timeout, and one hung request would hold every
#      later Yes or No behind it.
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 1
ANSWER=${AUROS_ANSWER_SCRIPT:-$(pwd)/rootfs/usr/lib/auros/answer.sh}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/answerreport.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT
fail=0
ok()  { printf '    %-70s ok\n' "$1"; }
bad() { fail=$((fail+1)); printf '    %-70s FAIL\n' "$1"; [ -z "${2:-}" ] || printf '        %s\n' "$2"; }

BIN=$TMP/bin; SBIN=$TMP/sbin; OUT=$TMP/out
mkdir -p "$BIN" "$SBIN" "$OUT"
# aurfirst request: the one word, here report:/dev/sdb1
printf '#!/bin/sh\n[ "$1" = request ] && echo "report:/dev/sdb1"\n' > "$SBIN/aurfirst"
# `ferry stick list`: one stick (hangs when FAKE_HANG=ferry)
cat > "$TMP/ferry" <<'EOF'
#!/bin/sh
[ "${FAKE_HANG:-}" != ferry ] || exec sleep 30
printf '/dev/sdb1\tSTICK (16 GB)\t16000000000\texfat\n'
EOF
cat > "$BIN/blkid" <<'EOF'
#!/bin/sh
[ "${FAKE_HANG:-}" != blkid ] || exec sleep 30
echo exfat
EOF
cat > "$BIN/mount" <<'EOF'
#!/bin/sh
[ "${FAKE_HANG:-}" != mount ] || exec sleep 30
exit "${FAKE_MOUNT_RC:-0}"
EOF
cat > "$BIN/umount" <<'EOF'
#!/bin/sh
[ "${FAKE_HANG:-}" != umount ] || exec sleep 30
exit "${FAKE_UMOUNT_RC:-0}"
EOF
cat > "$BIN/sync" <<'EOF'
#!/bin/sh
exit "${FAKE_SYNC_RC:-0}"
EOF
cat > "$TMP/reporter" <<'EOF'
#!/bin/sh
[ "${FAKE_HANG:-}" != reporter ] || exec sleep 30
exit "${FAKE_REPORTER_RC:-0}"
EOF
chmod 755 "$BIN"/* "$SBIN"/* "$TMP/ferry" "$TMP/reporter"

# run answer.sh once with the given FAKE_* settings; the answer it wrote is in $RES, the seconds taken in $SECS
RES=$TMP/res
run() { # VAR=value ...
    rm -f "$OUT/result"; : > "$TMP/log"
    t0=$(date +%s)
    env "$@" PATH="$BIN:$PATH" AUROS_BIN="$SBIN" AUROS_ANSWER_OUT="$OUT" AUROS_LOG="$TMP/log" \
        AUROS_FERRY="$TMP/ferry" AUROS_REPORTER="$TMP/reporter" AUROS_REPORT_MNT="$TMP/rmnt" \
        AUROS_REPORT_MOUNT="$BIN/mount" AUROS_REPORT_UMOUNT="$BIN/umount" \
        AUROS_REPORT_STEP=2 AUROS_REPORT_LIMIT=2 AUROS_STATE="$TMP/state" \
        sh "$ANSWER" >/dev/null 2>&1
    SECS=$(( $(date +%s) - t0 ))
    cat "$OUT/result" > "$RES" 2>/dev/null || : > "$RES"
}
says() { grep -q -- "$1" "$RES"; }
# case NAME  "VAR=.." ... -- result-line why-line-or-"" note-fragment
check() { # NAME RESULT WHY NOTE-FRAGMENT [VAR=value ...]
    name=$1; want=$2; why=$3; note=$4; shift 4
    run FAKE_NONE=1 "$@"
    good=1
    says "^request=report" || good=0
    says "^result=$want\$" || good=0
    if [ -n "$why" ]; then says "^why=$why\$" || good=0; fi
    if [ -n "$note" ]; then says "^note=.*$note" || good=0; else ! says "^note=" || good=0; fi
    if [ "$want" != ok ] && says "^result=ok"; then good=0; fi
    if [ "$good" = 1 ]; then ok "$name"; else bad "$name" "$(tr '\n' ' ' < "$RES")"; fi
}
# the same, and it must have been stopped by the clock, well inside the 30 seconds the fake would sleep
quick() { # NAME RESULT WHY NOTE-FRAGMENT [VAR=value ...]
    check "$@"
    [ "$SECS" -lt 20 ] && ok "...and it took $SECS s, not 30: the step was stopped" || bad "...it was stopped by the clock" "took $SECS s"
}

echo "  how a report ends, in her words"
check "written, flushed, let go of: ok, and no note" ok "" ""
check "the stick will not mount: it could not be opened" failed unopened "could not open the USB stick" FAKE_MOUNT_RC=32
check "the stick will not let go: failed, and NOT ok" failed unclosed "may not be on the USB stick" FAKE_UMOUNT_RC=16
check "the stick will not flush: failed, and NOT ok" failed unclosed "may not be on the USB stick" FAKE_SYNC_RC=5
check "no room on the stick: the same why=noroom as the files branch" failed noroom "not enough room on the USB stick for the report" FAKE_REPORTER_RC=4
says "^code=4\$" && ok "...with the code 4 ferry uses for it" || bad "...with code=4" "$(tr '\n' ' ' < "$RES")"
check "not a stick it may write on (3)" failed notstick "not a USB stick AurOS can save a report on" FAKE_REPORTER_RC=3
check "any other end of the reporter" failed "" "the report could not be saved on the USB stick" FAKE_REPORTER_RC=1
check "the reporter ends 124 (stopped by its clock)" failed "" "took far too long" FAKE_REPORTER_RC=124

echo "  a dying stick: every step has a clock"
quick "ferry's stick list hangs: said, not 'not there any more'" failed unopened "did not answer in time" FAKE_HANG=ferry
quick "blkid hangs: the stick could not be opened" failed unopened "could not open the USB stick" FAKE_HANG=blkid
quick "mount hangs: the stick could not be opened" failed unopened "could not open the USB stick" FAKE_HANG=mount
quick "umount hangs: failed, and NOT ok" failed unclosed "may not be on the USB stick" FAKE_HANG=umount
quick "the reporter hangs: stopped, said" failed "" "took far too long" FAKE_HANG=reporter

echo
[ "$fail" = 0 ] && { echo "  answerreporttest: all green"; exit 0; }
echo "  answerreporttest: $fail FAILED"; exit 1
