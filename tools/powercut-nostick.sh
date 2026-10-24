#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  powercut-nostick — the power cut, in the install that ships
#
#  tools/powercuttest.sh cuts the power at every named instant of the
#  install WITH a memory stick. The installer people download has no
#  stick (docs/AURBRIDGE.md, "Installing without a memory stick"):
#  the image lives in \AurOS\ on the Windows drive, the way back is
#  held in memory until the shrink has made room for it and is then
#  written onto this disk FIRST, and there is no record to resume
#  from. None of that had a power-cut test (docs/ATLAS.md section 2,
#  gap 3). This is it.
#
#  For each named instant (fault.h): the machine stops dead there, and
#    - above the line, the disk is byte-for-byte untouched;
#    - below it, every file in Windows (the AurOS folder too) is what
#      it was;
#    - before the commit, she does what the screen tells her: starts
#      Windows (which checks the drive -- emulated with ntfsfix -d) and
#      presses Install AurOS again. That second install must finish,
#      with Windows made smaller ONCE, not once per attempt (the retry
#      used to take another 16-40 GiB each time), and "Put Windows back"
#      afterwards must give back her whole Windows;
#    - after the commit, "Put Windows back" from the copy on this disk
#      gives back the machine she started with.
#
#    sudo AURSTAGE_FAULT=1 ./build/staging      # once
#    sudo sh tools/powercut-nostick.sh [instant ...]
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
. tools/machine.sh

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

IMG=out/auros-staging-fault.img
KERN=out/auros-staging-fault-vmlinuz
[ -f "$IMG" ] && [ -f "$KERN" ] || { echo "run: sudo AURSTAGE_FAULT=1 ./build/staging"; exit 2; }
[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
mach_need

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
MTMP=$(mktemp -d "${TMPDIR:-/tmp}/powercut-nostick.XXXXXX")
# A kill (timeout, ^C) must not leak the images in $TMP: the EXIT trap alone
# does not run on a signal in dash, and a leaked 3 GB disk image per run
# once filled homebase. INT and TERM exit, which runs the EXIT trap, and
# the background QEMU goes with it.
_a2_clean() { kill $(jobs -p) 2>/dev/null; mountpoint -q "$MTMP/m" 2>/dev/null && umount "$MTMP/m"; rm -rf "$MTMP" "$LOCK"; }
trap _a2_clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "With no memory stick: if the power goes at the worst instant, is"
echo "Windows still whole, and does trying again work?"
echo

mach_disk
mach_image

# ── AurBridge (no stick) arms this machine, with the fault build ────
SIM="$MTMP/sim"
mkdir -p "$SIM/esp" "$MTMP/winimg"
: > "$SIM/efivars.txt"
printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$DISK" "$(stat -c%s "$DISK")" \
    > "$SIM/disks.txt"
cp "$AIMG" "$MTMP/winimg/auros-desktop.img"
if out/aurbridge-sim "$SIM" desktop none "$MTMP/winimg/auros-desktop.img" \
       "$KERN" "$IMG" > "$MTMP/sim.out" 2> "$MTMP/sim.err" &&
   grep -q 'verdict=armed' "$MTMP/sim.out"; then
    ok "AurBridge (no stick) arms the machine"
else
    bad "AurBridge (no stick) arms the machine" "$(tail -3 "$MTMP/sim.err")"
    exit 1
fi
INITRD="$SIM/esp/EFI/AurOS/staging.img"

# The image and its manifest into \AurOS\ on the NTFS volume, where the
# wizard downloads them; the "before" picture includes them.
L=$(losetup --find --show -o $((P2S*512)) --sizelimit $(((P2E-P2S+1)*512)) "$DISK")
nt ntfs-3g "$L" "$MTMP/m" >/dev/null 2>&1 || { losetup -d "$L"; echo "  cannot write into NTFS"; exit 2; }
mkdir -p "$MTMP/m/AurOS"
cp "$MTMP/winimg/auros-desktop.img" "$MTMP/winimg/auros-desktop.img.manifest" "$MTMP/m/AurOS/"
( cd "$MTMP/m" && find . -type f -exec md5sum {} \; | sort ) > "$MTMP/win.before"
sync; umount "$MTMP/m"; losetup -d "$L"
WINFILES=$(wc -l < "$MTMP/win.before")
PRISTINE=$(md5sum "$DISK" | cut -d' ' -f1)
echo "  a 3 GiB machine, $WINFILES files in Windows including the AurOS image"

# ── one boot, ONE disk, no stick ────────────────────────────────────
boot() { # disk  kargs  marker  [timeout]
    cp --sparse=always "$1" "$MTMP/run.img"
    cp /usr/share/OVMF/OVMF_VARS_4M.fd "$MTMP/vars.fd"
    : > "$MTMP/out.txt"
    qemu-system-x86_64 -machine q35,accel=tcg -m 1536 -smp 2 \
        -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
        -drive if=pflash,format=raw,unit=1,file="$MTMP/vars.fd" \
        -no-reboot -kernel "$KERN" -initrd "$INITRD" -append "console=ttyS0 gpt $2" \
        -drive file="$MTMP/run.img",format=raw,if=none,id=d0 \
        -device virtio-blk-pci,drive=d0,serial=AUROSTEST \
        -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
        -display none -serial stdio > "$MTMP/out.txt" 2>&1 &
    _qp=$!; _seen=0; _i=0; _to=${4:-900}
    while [ "$_i" -lt "$_to" ]; do
        kill -0 "$_qp" 2>/dev/null || break
        if [ "$_seen" -eq 0 ] && grep -aq "$3" "$MTMP/out.txt" 2>/dev/null; then
            _seen=1; _i=$(( _to - 15 ))
        fi
        sleep 1; _i=$((_i+1))
    done
    kill -9 "$_qp" 2>/dev/null; wait "$_qp" 2>/dev/null
    grep -aq "$3" "$MTMP/out.txt"
}

p2() { # image  first|last
    sgdisk -i 2 "$1" 2>/dev/null | sed -n "s/^$2 sector: \([0-9]*\).*/\1/p"
}
F=First; La=Last

win_files_ok() { # image  label
    _s=$(p2 "$1" $F); _e=$(p2 "$1" $La)
    [ -n "$_s" ] && [ -n "$_e" ] || { bad "$2: Windows is still in the table"; return; }
    _l=$(losetup --find --show -o $((_s*512)) --sizelimit $(((_e-_s+1)*512)) "$1" 2>/dev/null)
    if [ -n "$_l" ] && nt ntfs-3g -o ro "$_l" "$MTMP/m" >/dev/null 2>&1; then
        ( cd "$MTMP/m" && find . -type f -exec md5sum {} \; | sort ) > "$MTMP/win.now"
        umount "$MTMP/m"
        cmp -s "$MTMP/win.before" "$MTMP/win.now" \
            && ok "$2: every file in Windows is exactly what it was" \
            || bad "$2: every file in Windows is exactly what it was" \
                   "$(diff "$MTMP/win.before" "$MTMP/win.now" | head -3)"
    else
        bad "$2: the Windows volume still mounts"
    fi
    [ -n "$_l" ] && losetup -d "$_l"
}

# What Windows starting does to a volume ntfsresize left marked for
# checking: chkdsk runs and clears the mark. ntfsfix -d is that step.
windows_checks_it() { # image
    _s=$(p2 "$1" $F); _e=$(p2 "$1" $La)
    _l=$(losetup --find --show -o $((_s*512)) --sizelimit $(((_e-_s+1)*512)) "$1") || return 1
    nt ntfsfix -d "$_l" >/dev/null 2>&1; _rc=$?
    losetup -d "$_l"; return $_rc
}

back_to_normal() { # image  label
    sgdisk -v "$1" 2>&1 | grep -q "No problems found" \
        && ok "$2: the table is valid" || bad "$2: the table is valid" "$(sgdisk -v "$1" 2>&1 | head -2)"
    _n=$(sgdisk -p "$1" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)
    [ "$_n" = 3 ] && ok "$2: the three original partitions are back" \
                  || bad "$2: the three original partitions are back" "it has $_n"
    [ "$(p2 "$1" $F)" = "$P2S" ] && [ "$(p2 "$1" $La)" = "$P2E" ] \
        && ok "$2: Windows' partition is its full size" \
        || bad "$2: Windows' partition is its full size" "$(p2 "$1" $F)..$(p2 "$1" $La)"
    _m=$(dd if="$1" bs=512 skip=$P1S count=$((P1E-P1S+1)) status=none | md5sum | cut -d' ' -f1)
    [ "$_m" = "$ESPMD5" ] && ok "$2: the EFI partition is byte-for-byte what it was" \
                          || bad "$2: the EFI partition is byte-for-byte what it was"
    _t=$(ntfs_total "$1" "$P2S" | cut -d' ' -f1); _d=$((NTFSTOT - _t))
    [ "$_d" -ge 0 ] && [ "$_d" -le "$NTFSSPC" ] \
        && ok "$2: the Windows FILESYSTEM is its full size" \
        || bad "$2: the Windows FILESYSTEM is its full size" "claims $_t, had $NTFSTOT"
    win_files_ok "$1" "$2"
}

# ── the reference: one ordinary install, uninterrupted ──────────────
echo
echo "  first, one ordinary install, to know what \"shrunk once\" looks like"
if boot "$DISK" "aurstage.install aurstage.min_gb=1" "INSTALLTEST-AUROS-STARTED"; then
    ok "it installs"
else
    bad "it installs" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
    echo; echo "nothing else here means anything without this."; exit 1
fi
cp --sparse=always "$MTMP/run.img" "$MTMP/installed.img"
REF_T=$(ntfs_total "$MTMP/run.img" "$P2S" | cut -d' ' -f1)
echo "    (Windows' filesystem after one install: $REF_T sectors, from $NTFSTOT)"

# ── what she is told when a step fails, in each state ──────────────
#
# aurstage.fail_at=<step> takes that step's own failure path (fault
# builds only). The sentence on the screen must be true for the state
# the disk is in: after the shrink it must not say "nothing on it has
# been changed"; after the commit, with BootNext on AurOS, it must not
# say Windows will start (docs/ATLAS.md section 2, gap 2).
if [ -z "${*:-}" ] || [ "${*:-}" = messages ]; then
    echo
    echo "  what the screen says when a step fails"
    told() { tr -d '\r' < "$MTMP/out.txt" | grep -a 'aurstage' ; }
    if boot "$DISK" "aurstage.install aurstage.min_gb=1 aurstage.fail_at=write" \
            "verdict=write-failed" 600; then
        ok "a failed copy, after the shrink: it stops"
        told | grep -q "nothing on it has been changed" \
            && bad "...and does not say nothing has been changed" "$(told | tail -6)" \
            || ok "...and does not say nothing has been changed"
        told | grep -q "less free space" \
            && ok "...and says Windows has less free space" \
            || bad "...and says Windows has less free space" "$(told | tail -6)"
    else
        bad "a failed copy, after the shrink: it stops" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
    fi
    if boot "$DISK" "aurstage.install aurstage.min_gb=1 aurstage.fail_at=settle" \
            "verdict=settle-failed" 600; then
        ok "a failure after the commit: it stops"
        told | grep -q "AurOS will start" \
            && ok "...and says AurOS starts next (BootNext is on it)" \
            || bad "...and says AurOS starts next (BootNext is on it)" "$(told | tail -6)"
        told | grep -q "Windows will start" \
            && bad "...and does not say Windows will start" "$(told | tail -6)" \
            || ok "...and does not say Windows will start"
    else
        bad "a failure after the commit: it stops" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
    fi
fi
[ "${*:-}" = messages ] && set --

POINTS="${*:-gate shrink-begin shrink-mid shrink-end mirror-end write-mid write-end boot-mid boot-end entry-written commit-array commit-sector commit-backup boot-entry settle-mid settle-end}"
[ "${ONLY:-}" = restore ] && POINTS=""
for pt in $POINTS; do
    printf '\n  ── %s\n' "$pt"
    if boot "$DISK" "aurstage.install aurstage.min_gb=1 aurstage.die_at=$pt" \
            "AURSTAGE-FAULT $pt" 600; then
        ok "the machine stops dead at $pt"
    else
        bad "the machine stops dead at $pt" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"
        continue
    fi
    cp --sparse=always "$MTMP/run.img" "$MTMP/cut.img"
    case "$pt" in
      gate|shrink-begin)
        [ "$(md5sum "$MTMP/cut.img" | cut -d' ' -f1)" = "$PRISTINE" ] \
            && ok "the disk is byte-for-byte untouched" \
            || bad "the disk is byte-for-byte untouched" ;;
      *) win_files_ok "$MTMP/cut.img" "after the cut" ;;
    esac

    case "$pt" in
      commit-sector|commit-backup|boot-entry|settle-mid|settle-end)
        # The new table is in force; the way back went onto this disk
        # before the image did.
        if boot "$MTMP/cut.img" "aurstage.restore" "verdict=restored" 900; then
            ok "Put Windows back, from the copy on this disk, works"
            back_to_normal "$MTMP/run.img" "after $pt and Put Windows back"
        else
            bad "Put Windows back, from the copy on this disk, works" \
                "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
        fi ;;
      *)
        # Before the commit: she starts Windows (it checks the drive)
        # and presses Install AurOS again.
        windows_checks_it "$MTMP/cut.img" \
            || bad "Windows can check the drive" "ntfsfix -d refused it"
        if boot "$MTMP/cut.img" "aurstage.install aurstage.min_gb=1" \
                "INSTALLTEST-AUROS-STARTED"; then
            ok "trying again installs AurOS"
        else
            bad "trying again installs AurOS" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
            continue
        fi
        _t=$(ntfs_total "$MTMP/run.img" "$P2S" | cut -d' ' -f1)
        [ "$_t" -ge "$((REF_T - NTFSSPC))" ] \
            && ok "...and Windows was made smaller once, not twice" \
            || bad "...and Windows was made smaller once, not twice" \
                   "its filesystem is $_t sectors; one install leaves $REF_T"
        win_files_ok "$MTMP/run.img" "after trying again"
        cp --sparse=always "$MTMP/run.img" "$MTMP/again.img"
        if boot "$MTMP/again.img" "aurstage.restore" "verdict=restored" 900; then
            ok "...and Put Windows back after it works"
            back_to_normal "$MTMP/run.img" "after $pt, again, and Put Windows back"
        else
            bad "...and Put Windows back after it works" \
                "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
        fi ;;
    esac
done

# ── the power goes during Put Windows back ──────────────────────────
#
# The restore is run from AurOS, and once it has rewritten the partition
# table the partition AurOS starts from is gone and nothing can start it
# again (docs/safety, finding on rescue.c). So it does the part that needs
# AurOS first -- the Windows startup files, and only the megabytes that
# differ -- then the table, and last the filesystem, which Windows can
# finish by itself. A cut before the table: AurOS is still in the table and
# running the restore again finishes it. A cut after: Windows starts with
# the right table and every file, and the only thing left is room.
RESTORE_POINTS="${RESTORE_POINTS:-restore-esp-mid restore-array restore-sector restore-backup restore-grow}"
if [ -z "${*:-}" ]; then
  echo
  echo "  the power goes during Put Windows back"
  # Windows has changed its startup files since the install: two places
  # in the EFI partition differ from what was saved, one before the
  # middle of the restore and one after, so a cut in the middle leaves it
  # half done.
  tear_esp() { python3 - "$1" "$((P1S*512))" <<'EOPY'
import sys, os
f = open(sys.argv[1], 'r+b'); base = int(sys.argv[2])
for mb in (20, 80):
    f.seek(base + mb * 1048576 + 4096); f.write(os.urandom(4096))
f.close()
EOPY
  }
  for pt in $RESTORE_POINTS; do
    printf '\n  ── %s\n' "$pt"
    cp --sparse=always "$MTMP/installed.img" "$MTMP/torn.img"
    tear_esp "$MTMP/torn.img"
    [ "$(dd if="$MTMP/torn.img" bs=512 skip=$P1S count=$((P1E-P1S+1)) status=none | md5sum | cut -d' ' -f1)" != "$ESPMD5" ] \
        && ok "(Windows has changed its startup files since the install)" \
        || bad "(Windows has changed its startup files since the install)"
    if boot "$MTMP/torn.img" "aurstage.restore aurstage.die_at=$pt" "AURSTAGE-FAULT $pt" 900; then
        ok "the machine stops dead at $pt"
    else
        bad "the machine stops dead at $pt" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"
        continue
    fi
    cp --sparse=always "$MTMP/run.img" "$MTMP/cut.img"
    win_files_ok "$MTMP/cut.img" "after the cut"
    case "$pt" in
      restore-esp-mid|restore-array)
        # AurOS is still in the table: run it again.
        _n=$(sgdisk -p "$MTMP/cut.img" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)
        [ "$_n" = 6 ] && ok "AurOS is still in the table (6 partitions): it can be run again" \
                      || bad "AurOS is still in the table, so it can be run again" "it has $_n"
        if boot "$MTMP/cut.img" "aurstage.restore" "verdict=restored" 900; then
            ok "running it again finishes the job"
            back_to_normal "$MTMP/run.img" "after $pt, again"
        else
            bad "running it again finishes the job" "$(grep -a 'aurstage' "$MTMP/out.txt" | grep -av 'drivers\|loaded' | tail -9)"
        fi ;;
      *)
        # The table is back; the partition AurOS started from is gone.
        # Windows starts (and checks its drive).
        _n=$(sgdisk -p "$MTMP/cut.img" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)
        [ "$_n" = 3 ] && ok "the original three partitions are back" \
                      || bad "the original three partitions are back" "it has $_n"
        windows_checks_it "$MTMP/cut.img" || true
        [ "$(dd if="$MTMP/cut.img" bs=512 skip=$P1S count=$((P1E-P1S+1)) status=none | md5sum | cut -d' ' -f1)" = "$ESPMD5" ] \
            && ok "the EFI partition is byte-for-byte what it was (restored before the table)" \
            || bad "the EFI partition is byte-for-byte what it was" "the table came first"
        # At restore-sector the spare copy still describes the AurOS layout
        # (it catches up at restore-backup): the main table is whole and
        # is what every reader uses.
        sgdisk -v "$MTMP/cut.img" 2>&1 | grep -qE "No problems found|Main partition table: OK|Main and backup partition tables differ" \
            && ok "the table is valid (the spare copy catches up next)" || bad "the table is valid" "$(sgdisk -v "$MTMP/cut.img" 2>&1 | head -2)"
        win_files_ok "$MTMP/cut.img" "Windows starting" ;;
    esac
  done

  # ── and a partition that was not AurOS's ────────────────────────────
  # Put Windows back writes the saved table over the live one. Anything on
  # the disk that is neither AurOS's nor in the saved table would be
  # erased, and Windows' drive grown over it: a recovery partition
  # Windows Update made or resized after the install, one a helper added.
  # The restore must refuse, before changing a byte.
  echo
  echo "  ── a partition that was not AurOS's"
  for kind in new changed; do
    cp --sparse=always "$MTMP/installed.img" "$MTMP/foreign.img"
    if [ "$kind" = new ]; then
        # Free the last 200 MB of AurOS's root and make a partition of it
        # that holds somebody's data.
        set -- $(mach_part "$MTMP/foreign.img" AUROS-ROOT)
        RN=$1; RF=$2; RL=$3
        FF=$((RL - 409600 + 1))
        sgdisk -d "$RN" -n "$RN:$RF:$((FF - 1))" -t "$RN:8304" -c "$RN:AUROS-ROOT" "$MTMP/foreign.img" >/dev/null 2>&1
        sgdisk -n "7:$FF:$RL" -t 7:0700 -c 7:SOMEBODYS "$MTMP/foreign.img" >/dev/null 2>&1
        python3 - "$MTMP/foreign.img" "$((FF*512))" <<'EOPY'
import sys
f = open(sys.argv[1], 'r+b'); f.seek(int(sys.argv[2]))
f.write(b'IRREPLACEABLE-' * 4000); f.close()
EOPY
        label="a partition added after the install"
    else
        # Windows Update resized WinRE after the install.
        U=$(sgdisk -i 3 "$MTMP/foreign.img" | sed -n 's/^Partition unique GUID: //p')
        WRF=$(sgdisk -i 3 "$MTMP/foreign.img" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
        WRL=$(sgdisk -i 3 "$MTMP/foreign.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
        sgdisk -d 3 -n "3:$WRF:$((WRL - 4096))" -t 3:2700 -u "3:$U" -c 3:WinRE "$MTMP/foreign.img" >/dev/null 2>&1
        label="a recovery partition resized after the install"
    fi
    BEFORE=$(md5sum "$MTMP/foreign.img" | cut -d' ' -f1)
    # aurstage.nolog: e85deba made staging write a log to the EFI partition with its
    # verdict; this asserts the refusal changed nothing at all.
    boot "$MTMP/foreign.img" "aurstage.restore aurstage.nolog" "aurstage-report v1 verdict=restore" 600 || true
    grep -aq "verdict=restored" "$MTMP/out.txt" \
        && bad "$label: Put Windows back refuses" "it ran" \
        || ok "$label: Put Windows back refuses"
    grep -aq "has changed since AurOS was installed" "$MTMP/out.txt" \
        && ok "...and says why, in words" || bad "...and says why, in words" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -5)"
    [ "$(md5sum "$MTMP/run.img" | cut -d' ' -f1)" = "$BEFORE" ] \
        && ok "...and the disk is byte for byte untouched" || bad "...and the disk is byte for byte untouched"
  done
fi

echo
if [ "$fail" -gt 0 ]; then
    echo "$fail of $checked wrong. Each is somebody's Windows."
    exit 1
fi
echo "$checked checks: with no stick, a power cut at any named instant"
echo "leaves Windows whole, and trying again or putting Windows back works."
exit 0
