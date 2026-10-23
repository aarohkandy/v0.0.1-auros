#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  theftunit — the encrypted drive and its PIN, on a file, against a
#  software security chip (docs/security/THEFT.md)
#
#  Runs the real pieces -- rootfs/usr/lib/auros/lock/{encrypt-init,
#  lockctl} and src/aurshell/lockpin.c, compiled here -- on a loop
#  device, with swtpm as the chip:
#
#    in place     a plain ext4 with her files becomes LUKS2; opens with
#                 the temporary key while mounted; the pass finishes;
#                 no byte of the old plaintext is left on the raw file;
#                 a pass killed half-way resumes and finishes
#    first PIN    set-pin seals it; the rescue code is 24 numbers, made
#                 fresh each time; the temporary key is gone; the start
#                 page's own code opens with the PIN
#    wrong PIN    says "wrong", and the chip's own counter rises
#    too many     the chip locks out, the page says so (not "wrong"),
#                 and the right PIN does not get through until it clears
#    rescue       the code opens the drive; the page's flow after it
#                 hands the new PIN to `lockctl repin`, which seals it
#    PCR 7        bound to no PCR (the default): a firmware/dbx change is
#                 a non-event. Bound to PCR 7: the page says "changed",
#                 the rescue code opens, repin re-seals, the PIN works
#    new chip     a different (cleared) chip: "changed", rescue, repin
#    no chip      the PIN is a passphrase, 6 numbers or refused
#    change-pin, new-rescue-code (both kinds); EASY.md: every key and
#    button at least 44 px at 1024x600, no jargon in the page's words
#
#  Root, loop devices and device-mapper: run it in a throwaway container
#  (the host's own TPM is never touched -- every call is pointed at the
#  swtpm, and the container's /dev/tpm* nodes are removed first):
#
#    docker run --rm --privileged -v "$PWD":/w -w /w IMAGE bash tools/theftunit.sh
#
#  where IMAGE is ubuntu:24.04 plus cryptsetup-bin libcryptsetup-dev
#  systemd swtpm swtpm-tools tpm2-tools libtss2-tcti-swtpm0t64 gcc
#  e2fsprogs util-linux fonts-dejavu-core (what .github/workflows/theft.yml installs).
#
#  THEFT_SRC=DIR runs a mutated copy of the tree instead (the mutation
#  run: tools/theftunit.sh --mutants proves each check can fail).
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
TOP=$PWD

if [ "${1:-}" = --mutants ]; then
    # Each mutant breaks one thing a check above claims to catch. The
    # suite must FAIL on every one of them.
    M=$(mktemp -d "${TMPDIR:-/tmp}/theftmut.XXXXXX")
    trap 'rm -rf "$M"' EXIT
    trap 'exit 130' INT TERM       # the EXIT trap then cleans up (FIELD-MANUAL, disk budget)
    survived=0
    mutant() { # NAME FILE SED
        rm -rf "$M/t"; mkdir -p "$M/t"
        cp -a src rootfs tools "$M/t/"
        sed -i "$3" "$M/t/$2"
        if cmp -s "$M/t/$2" "$2"; then echo "  mutant $1: the edit did not apply"; survived=$((survived+1)); return; fi
        if THEFT_SRC="$M/t" THEFT_QUICK=1 bash "$0" >"$M/log" 2>&1; then
            echo "  mutant $1: SURVIVED (the suite passed with it)"; survived=$((survived+1))
        else
            echo "  mutant $1: killed ($(grep -c FAIL "$M/log") checks failed)"
        fi
    }
    mutant wrong-is-changed   src/aurshell/lockpin.c 's/after.counter > before.counter/0/'
    mutant lockout-unseen     src/aurshell/lockpin.c 's/if (after.in_lockout) return V_LOCKED;//'
    mutant temp-kept          rootfs/usr/lib/auros/lock/lockctl 's/^    temp_forget "$dev" "$code" ||/    true ||/'
    mutant no-reencrypt       rootfs/usr/lib/auros/lock/encrypt-init 's/--init-only --batch-mode/--init-only --batch-mode --test-passphrase-nope/'
    mutant fixed-rescue       rootfs/usr/lib/auros/lock/lockctl 's/\$((b % 10))/1/'
    mutant pin-unchecked      rootfs/usr/lib/auros/lock/lockctl 's/if ! pin_checks "\$dev" "\$pin"; then/if false; then/'
    mutant header-unchecked rootfs/usr/lib/auros/lock/encrypt-init 's/if \[ "\$got" = "\$ss" \]; then/if true; then/'
    mutant needs-head rootfs/usr/lib/auros/lock/encrypt-init 's/got=\${1:-}/got=$(echo "${1:-}" | head -1)/'
    mutant sector-always-4096 rootfs/usr/lib/auros/lock/encrypt-init 's/else ss=512; fi/else ss=4096; fi/'
    # the VM harness's own verdict (run alone: THEFT_ONLY=vmself)
    vmmutant() { # NAME SED
        rm -rf "$M/t"; mkdir -p "$M/t"; cp -a src rootfs tools "$M/t/"
        sed -i "$2" "$M/t/tools/theftvm.sh"
        if cmp -s "$M/t/tools/theftvm.sh" tools/theftvm.sh; then echo "  mutant $1: the edit did not apply"; survived=$((survived+1)); return; fi
        if THEFT_SRC="$M/t" THEFT_ONLY=vmself bash "$0" >"$M/log" 2>&1; then
            echo "  mutant $1: SURVIVED (the suite passed with it)"; survived=$((survived+1))
        else echo "  mutant $1: killed ($(grep -c FAIL "$M/log") checks failed)"; fi
    }
    vmmutant harness-no-poweroff  '/\[poweroff\]/s/grep -q "THEFT poweroff"/true/'
    vmmutant harness-no-done      '/\[done\]/s/grep -q "THEFT pass done" &&/true \&\&/'
    vmmutant harness-no-timeout   '/\[timeout\]/s/! printf .*grep -q "THEFT pass TIMEOUT" &&/true \&\&/'
    mutant stub-no-exit       rootfs/usr/lib/auros/lock/lockctl '/^exit$/s/exit/:/'
    mutant stub-no-else       rootfs/usr/lib/auros/lock/lockctl 's/AurOS is not on this computer any more/AurOS is here/'
    mutant stub-menu-no-exit  rootfs/usr/lib/auros/lock/lockctl '/configfile (\\$root)\/auros\/grub.cfg$/{n;d;}'
    mutant convert-no-lock    rootfs/usr/lib/auros/lock/lockctl 's/ auros\.lock=PARTUUID=\$partuuid#/#/'
    mutant small-keys         src/aurshell/lockpin.c 's/int kw = (int)(84 \* k)/int kw = (int)(40 * k)/'
    mutant repin-ignored      rootfs/usr/lib/auros/lock/lockctl 's/^    \[ -s "\$REPIN" \] || exit 0/    exit 0/'
    echo
    [ "$survived" = 0 ] && { echo "every mutant was caught"; exit 0; }
    echo "$survived mutant(s) survived"; exit 1
fi

SRC=${THEFT_SRC:-$TOP}
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

# The VM test's own verdict on a pass: it must refuse a guest that timed
# out, was killed at the cap, or never powered off.
vmself_check() {
    local o
    o=$(bash "$SRC/tools/theftvm.sh" --selftest 2>&1); local rc=$?
    [ "$rc" = 0 ] && echo "$o" | grep -q "a guest killed at the wait cap" \
        && ok "the VM test refuses a pass that timed out, was killed at the cap, or never powered off" \
        || bad "the VM test's pass verdict" "$o"
}
if [ "${THEFT_ONLY:-}" = vmself ]; then
    echo; echo "The VM test's own verdict"; echo
    vmself_check
    [ "$fail" -eq 0 ] && exit 0 || exit 1
fi
check() { local what=$1; shift; if "$@" >/dev/null 2>&1; then ok "$what"; else bad "$what"; fi; }

# ── never the host's chip ─────────────────────────────────────────
[ "$(id -u)" = 0 ] || { echo "theftunit needs root (run it in the container)"; exit 2; }
if [ -e /.dockerenv ] || grep -qa container= /proc/1/environ 2>/dev/null; then
    rm -f /dev/tpm0 /dev/tpmrm0 /dev/tpm1 /dev/tpmrm1
    # A container's /dev is a snapshot: on a busy host the free loop
    # devices have high numbers it has no nodes for ("failed to set up
    # loop device: No such file or directory").
    for i in $(seq 0 127); do [ -e /dev/loop$i ] || mknod /dev/loop$i b 7 $i 2>/dev/null; done
fi
ls /dev/tpm* >/dev/null 2>&1 && { echo "a real TPM is visible here; refusing (run in a container)"; exit 2; }
for t in cryptsetup swtpm tpm2_pcrextend gcc mkfs.ext4 losetup systemd-cryptenroll; do
    command -v "$t" >/dev/null || { echo "need $t"; exit 2; }
done

T=$(mktemp -d "${TMPDIR:-/tmp}/theft.XXXXXX")
ID=s4t$$$(od -An -tx1 -N3 /dev/urandom | tr -d ' ')
LOOPS=""; NAMES=""; SWPID=""
cleanup() {
    set +e
    # An in-flight pass is stopped the clean way (SIGTERM, at the end of
    # its hot zone) and waited for: a pass torn down mid-hot-zone leaves a
    # suspended device-mapper device, and every sync on the host then
    # hangs (this froze a shared server twice).
    pkill -TERM -f "cryptsetup reencrypt" 2>/dev/null
    for _ in $(seq 1 120); do pgrep -f "cryptsetup reencrypt" >/dev/null 2>&1 || break; sleep 1; done
    umount "$T/m" 2>/dev/null
    for n in $NAMES; do cryptsetup close "$n" 2>/dev/null; done
    for l in $LOOPS; do losetup -d "$l" 2>/dev/null; done
    for f in "$T"/*.img; do       # and anything else still holding our files
        for l in $(losetup -n -O NAME -j "$f" 2>/dev/null); do losetup -d "$l"; done
    done
    [ -n "$SWPID" ] && kill "$SWPID" 2>/dev/null
    rm -rf "$T" /run/auros-lock
}
trap cleanup EXIT
trap 'exit 130' INT TERM   # a killed run must not leave multi-GB images or loop devices

PIN="$T/aurlock-pin"
gcc -O2 -std=gnu11 -Wall -Wextra -Werror -o "$PIN" "$SRC/src/aurshell/lockpin.c" \
    "$SRC/src/aurshell/kms.c" "$SRC/src/aurshell/draw.c" "$SRC/src/common/font.c" \
    "$SRC/src/common/png.c" "$SRC/src/common/theme.c" -lm -ldl \
    || { echo "aurlock-pin did not build"; exit 1; }
LOCKCTL="$SRC/rootfs/usr/lib/auros/lock/lockctl"
ENCINIT="$SRC/rootfs/usr/lib/auros/lock/encrypt-init"
mkdir -p "$T/m"

# ── the chip ──────────────────────────────────────────────────────
tpm_start() { # STATE-DIR
    mkdir -p "$1"
    swtpm socket --tpm2 --tpmstate dir="$1" --server type=unixio,path="$1/sock" \
        --ctrl type=unixio,path="$1/sock.ctrl" --flags startup-clear --pid file="$1/pid" --daemon
    for _ in 1 2 3 4 5 6 7 8 9 10; do [ -S "$1/sock" ] && break; sleep 0.2; done
    SWPID=$(cat "$1/pid")
    export TPM2TOOLS_TCTI="swtpm:path=$1/sock" AUROS_LOCK_TPM="swtpm:path=$1/sock"
    export AURLOCK_TPM2_DEVICE="$AUROS_LOCK_TPM"
    # Windows' policy is 32 tries then one per 10 minutes; 5 and 4 s
    # here, so that locking out and coming back fit in a test.
    tpm2_dictionarylockout -s -n 5 -t 4 -l 4 >/dev/null 2>&1
}
tpm_stop() { kill "$SWPID" 2>/dev/null; sleep 0.3; SWPID=""; }
counter() { "$PIN" --tpm-status | sed -n 's/^counter=//p'; }
tpm_start "$T/tpm1"

# ── a drive with her files on it ──────────────────────────────────
# Sets DRV to the loop device. Not a $(...), for the same reason as below.
mkdrive() { # FILE MB [EXTRA-BYTES [LOGICAL-BLOCK-SIZE]]
    truncate -s "$(( $2 * 1048576 + ${3:-0} ))" "$1"
    local l; l=$(losetup -f --show ${4:+--sector-size $4} "$1") || return 1
    LOOPS="$LOOPS $l"; DRV=$l
    mkfs.ext4 -q -L AUROS-ROOT "$l"
    mount "$l" "$T/m"
    for i in $(seq 1 400); do echo "HER-LETTER-PLAINTEXT-$i"; done > "$T/m/letter.txt"
    dd if=/dev/urandom of="$T/m/photo" bs=1M count=$(( $2 / 4 )) status=none
    sha256sum "$T/m/photo" | cut -c1-64 > "$1.sum"
    umount "$T/m"
}
# Sets V to the last verdict; on "open" the drive is left open as $LAST.
# Not a $(...): the names have to reach this shell for the clean-up.
open_try() { # DEV ANSWERS...
    local n="$ID-$RANDOM"
    printf '%s\n' "${@:2}" | "$PIN" --try "$1" "$n" > "$T/v" 2>/dev/null
    V=$(tail -1 "$T/v")
    LAST=""
    [ -b "/dev/mapper/$n" ] && { NAMES="$NAMES $n"; LAST=$n; }
}
close_last() { umount "$T/m" 2>/dev/null; [ -n "$LAST" ] && cryptsetup close "$LAST" 2>/dev/null; LAST=""; }
L() { AUROS_LOCK_DEV="$1" AUROS_LOCK_PIN_BIN="$PIN" AUROS_LOCK_CONF="$T/lock.conf" bash "$LOCKCTL" "${@:2}"; }

echo
echo "The encrypted drive and its PIN"
echo
vmself_check
echo "  in place"
IMG="$T/a.img"
mkdrive "$IMG" 1400; D=$DRV
# Run with ONLY the tools an initramfs has (no head, tail, tr, cut, awk,
# od...): the first VM run found "head: not found" in the real one.
mkdir -p "$T/rb"
for t in sh cat sed grep printf mkdir sleep cryptsetup blkid blockdev dumpe2fs e2fsck resize2fs modprobe; do
    p=$(command -v "$t") && ln -sf "$p" "$T/rb/$t"
done
ln -sf "$(command -v bash)" "$T/rb/bash"
enc_init() { env -i PATH="$T/rb" AUROS_LOCK_NOAES="${AUROS_LOCK_NOAES:-0}" bash "$ENCINIT" "$@" 2>&1; }
out=$(enc_init "$D"); rc=$?
[ "$rc" = 0 ] && ! echo "$out" | grep -q 'not found' && echo "$out" | grep -q 'as asked' \
    && ok "encrypt-init runs with only the tools an initramfs has, and its header check passes" \
    || bad "encrypt-init with only the initramfs's tools" "rc=$rc $out"
check "encrypt-init made the partition LUKS2" cryptsetup isLuks --type luks2 "$D"
open_try "$D" x; v=$V
[ "$v" = "open temp" ] && ok "it opens with the temporary key, no PIN asked" || bad "it opens with the temporary key" "got: $v"
if [ -b "/dev/mapper/$LAST" ]; then
    mount "/dev/mapper/$LAST" "$T/m"
    [ "$(sha256sum "$T/m/photo" | cut -c1-64)" = "$(cat "$IMG.sum")" ] \
        && ok "her files are intact, mid-encryption" || bad "her files are intact, mid-encryption"
    printf '1234\n' | L "$D" set-pin >/dev/null 2>&1; rc=$?
    [ "$rc" = 3 ] && ok "set-pin waits while the pass runs (exit 3, nothing changed)" || bad "set-pin waits for the pass" "exit $rc"
    # A pass stopped half-way (a shutdown), then resumed. SIGTERM, never
    # SIGKILL: killed between the suspend and the resume of its hot
    # zone, cryptsetup leaves a device-mapper device suspended, and
    # every `sync` on the HOST then hangs -- this froze a shared server
    # once. The power cut proper is the VM's test (tools/theftvm.sh).
    L "$D" encrypt-pass >/dev/null 2>&1 &
    sleep 1.5
    pkill -TERM -f "cryptsetup reencrypt --resume-only" 2>/dev/null
    wait
    L "$D" status | grep -qx state=encrypting && ok "a pass stopped half-way leaves it encrypting" \
        || ok "the pass finished before it could be stopped (fast disk)"
    L "$D" encrypt-pass >/dev/null 2>&1
    L "$D" status | grep -qx state=open-no-pin && ok "the pass resumes and finishes; state open-no-pin" \
        || bad "the pass resumes and finishes" "$(L "$D" status | tr '\n' ' ')"
    [ "$(sha256sum "$T/m/photo" | cut -c1-64)" = "$(cat "$IMG.sum")" ] \
        && ok "her files are intact after the pass" || bad "her files are intact after the pass"
    close_last
fi
n=$(grep -ac HER-LETTER-PLAINTEXT "$IMG")
[ "$n" = 0 ] && ok "no plaintext of hers is left on the raw drive" || bad "no plaintext left on the raw drive" "$n lines found"
cryptsetup luksDump "$D" | grep -q 'aes-xts-plain64' && ok "AES-XTS on a CPU with AES instructions" \
    || bad "AES-XTS on a CPU with AES instructions"

# A partition whose size is not a multiple of 4096 (the first VM run
# found real ones: cryptsetup refuses 4096-byte sectors on them).
mkdrive "$T/u.img" 1100 512; DU=$DRV
"$ENCINIT" "$DU" >/dev/null 2>&1
cryptsetup isLuks --type luks2 "$DU" && cryptsetup luksDump "$DU" | grep -qE 'sector: *512' \
    && ok "a partition not a multiple of 4096 bytes encrypts, with 512-byte sectors" \
    || bad "a partition not a multiple of 4096 bytes encrypts" "$(cryptsetup luksDump "$DU" 2>&1 | grep -E 'sector|not' | head -2)"
cryptsetup luksDump "$D" | grep -qE 'sector: *4096' && ok "a 4096-aligned one gets 4096-byte sectors" || bad "a 4096-aligned one gets 4096-byte sectors"
# A CPU without AES instructions: Adiantum, and the keyslot has to be
# usable too (the VM run: "Cannot use ... cipher for keyslot encryption").
mkdrive "$T/n.img" 1100 0; DNA=$DRV
out=$(AUROS_LOCK_NOAES=1 enc_init "$DNA")
cryptsetup isLuks "$DNA" && cryptsetup luksDump "$DNA" | grep -q 'xchacha12,aes-adiantum-plain64' \
    && ok "without AES instructions the drive is Adiantum" || bad "without AES instructions the drive is Adiantum" "$out"
# The size is chosen from the device, said out loud, and checked in the header.
mkdrive "$T/v.img" 1100 0; DV=$DRV
out=$("$ENCINIT" "$DV" 2>&1)
echo "$out" | grep -q 'sector size 4096 (the device.s logical block size is 512' && echo "$out" | grep -q 'header sector size 4096: as asked' \
    && ok "encrypt-init says which sector size it chose, and why, and checks the header" \
    || bad "encrypt-init says which sector size it chose, and checks the header" "$out"
# a 4Kn disk: the sector size cannot go below its logical block size
if mkdrive "$T/w.img" 1100 0 4096 2>/dev/null; then
    DW=$DRV; out=$("$ENCINIT" "$DW" 2>&1)
    echo "$out" | grep -q 'logical block size is 4096' && cryptsetup luksDump "$DW" | grep -qE 'sector: *4096' \
        && ok "a 4096-byte-block disk is encrypted with 4096-byte sectors" || bad "a 4096-byte-block disk" "$out"
else ok "(this kernel has no 4096-byte loop devices: the 4Kn case is not run here)"; fi
# the header disagreeing with the request: a cryptsetup that quietly
# writes 512-byte sectors. Exit 2, loudly, the partition still LUKS.
mkdir -p "$T/fake"
REALCS=$(command -v cryptsetup)
cat > "$T/fake/cryptsetup" <<FAKE
#!/bin/sh
p=""
for a do
    shift
    if [ "\$p" = "--sector-size" ]; then n=512; else n=\$a; fi
    p=\$a
    set -- "\$@" "\$n"
done
exec $REALCS "\$@"
FAKE
chmod 755 "$T/fake/cryptsetup"
mkdrive "$T/x.img" 1100 0; DX=$DRV
out=$(PATH="$T/fake:$PATH" "$ENCINIT" "$DX" 2>&1); rc=$?
[ "$rc" = 2 ] && cryptsetup isLuks "$DX" && echo "$out" | grep -q 'ERROR: the header has sector size' \
    && ok "a header that disagrees with the request is exit 2, loud, and still a LUKS drive" \
    || bad "a header that disagrees is caught" "rc=$rc $out"

echo
echo "  the first PIN (sealed in the chip, bound to no PCR)"
# A PIN that does not prove it opens the drive is rolled back whole:
# no rescue code, no sealed PIN, the temporary key still the only key.
printf '#!/bin/sh\n[ "$1" = --check ] && { cat >/dev/null; echo wrong; exit 1; }\nexec %s "$@"\n' "$PIN" > "$T/pinfail"
chmod 755 "$T/pinfail"
out=$(printf '1234\n' | AUROS_LOCK_DEV="$D" AUROS_LOCK_PIN_BIN="$T/pinfail" AUROS_LOCK_CONF="$T/lock.conf" bash "$LOCKCTL" set-pin 2>&1); rc=$?
slots=$(cryptsetup luksDump "$D" | sed -n 's/^  \([0-9]*\): luks2$/\1/p' | tr '\n' ' ')
[ "$rc" != 0 ] && ! echo "$out" | grep -qE '^[0-9]{4} ' && [ "$slots" = "0 " ] && L "$D" status | grep -qx state=open-no-pin \
    && ok "a PIN that does not prove itself is rolled back: no code, only the temporary key" \
    || bad "a PIN that does not prove itself is rolled back" "rc=$rc slots='$slots' out='$out'"
code=$(printf '1234\n' | L "$D" set-pin 2>"$T/err")
echo "$code" | grep -qE '^[0-9]{4}( [0-9]{4}){5}$' && ok "set-pin prints a rescue code of 6 groups of 4 numbers" \
    || bad "set-pin prints a rescue code" "got '$code' $(cat "$T/err")"
CODE=$(echo "$code" | tr -d ' ')
L "$D" status | grep -qx state=pin && ok "state pin" || bad "state pin" "$(L "$D" status | tr '\n' ' ')"
L "$D" status | grep -qx pin=tpm && ok "the PIN is sealed in the chip" || bad "the PIN is sealed in the chip"
! cryptsetup open --test-passphrase --key-file <(printf '%s' 'AurOS: no PIN has been chosen yet') "$D" 2>/dev/null \
    && ok "the temporary key no longer opens the drive" || bad "the temporary key no longer opens the drive"
open_try "$D" 1234; v=$V; [ "$v" = open ] && ok "the right PIN opens it (the start page's code)" || bad "the right PIN opens it" "got $v"
close_last
c0=$(counter)
open_try "$D" 9999; v=$V; c1=$(counter)
[ "$v" = wrong ] && ok "a wrong PIN says wrong" || bad "a wrong PIN says wrong" "got $v"
[ "$c1" -gt "$c0" ] && ok "the chip itself counted it ($c0 -> $c1)" || bad "the chip counted it" "$c0 -> $c1"
code2=$(printf '1234\n' | L "$D" set-pin 2>/dev/null)
[ -z "$code2" ] && ok "set-pin refuses when a PIN exists" || bad "set-pin refuses when a PIN exists"

echo
echo "  too many tries"
# The try that makes the chip lock out must itself say "locked", not "wrong".
v=""; lk=""
for _ in 1 2 3 4 5 6; do
    open_try "$D" 1111; v=$V
    lk=$("$PIN" --tpm-status | sed -n 's/^in_lockout=//p')
    [ "$lk" = 1 ] && break
done
[ "$lk" = 1 ] && [ "$v" = locked ] && ok "the try that locks the chip out already says so" \
    || bad "the try that locks the chip out already says so" "locked=$lk, that try said: $v"
open_try "$D" 1234; v=$V; [ "$v" = locked ] && ok "the right PIN is not let through while locked" || bad "locked even for the right PIN" "got $v"
close_last
sleep 9
open_try "$D" 1234; v=$V; [ "$v" = open ] && ok "after the wait the right PIN opens it" || bad "after the wait the right PIN opens it" "got $v"
close_last

echo
echo "  the rescue code"
open_try "$D" "rescue $CODE"; v=$V; [ "$v" = open ] && ok "the rescue code opens it" || bad "the rescue code opens it" "got $v"
close_last
open_try "$D" "rescue 000011112222333344445555"; v=$V; [ "$v" = wrong ] && ok "a wrong rescue code says wrong" || bad "a wrong rescue code" "got $v"
code_b=$(L "$D" new-rescue-code <<<"1234" 2>/dev/null | tr -d ' ')
[ ${#code_b} = 24 ] && [ "$code_b" != "$CODE" ] && ok "new-rescue-code makes a different code" || bad "new-rescue-code" "got '$code_b'"
open_try "$D" "rescue $CODE"; v=$V; [ "$v" = wrong ] && ok "the old rescue code stops working" || bad "the old rescue code stops working" "got $v"
close_last
open_try "$D" "rescue $code_b"; v=$V; [ "$v" = open ] && ok "the new rescue code works" || bad "the new rescue code works" "got $v"
close_last
CODE=$code_b

echo
echo "  forgot the PIN: the page's own flow, then repin"
n="$ID-page"
out=$( { echo link; echo "$CODE"; echo 2468; echo ok; echo 2468; echo ok; } | "$PIN" --script "$D" "$n" 2>/dev/null)
[ -b "/dev/mapper/$n" ] && NAMES="$NAMES $n" && LAST=$n
echo "$out" | grep -q 'page=newpin ' && echo "$out" | tail -1 | grep -q 'page=done' \
    && ok "link -> rescue code -> choose PIN -> once more -> open" || bad "the forgot-PIN flow" "$(echo "$out" | tr '\n' '|')"
[ -s /run/auros-lock/repin ] && [ "$(stat -c %a /run/auros-lock/repin)" = 600 ] \
    && ok "the new PIN is handed over in /run, readable by root only" || bad "the PIN is handed over in /run"
close_last
L "$D" repin >/dev/null 2>&1
[ ! -e /run/auros-lock/repin ] && ok "repin deletes the hand-over at once" || bad "repin deletes the hand-over"
open_try "$D" 2468; v=$V; [ "$v" = open ] && ok "the new PIN opens it" || bad "the new PIN opens it" "got $v"
close_last
open_try "$D" 1234; v=$V; [ "$v" = wrong ] && ok "the old PIN does not" || bad "the old PIN does not" "got $v"
close_last
tpm2_dictionarylockout -c >/dev/null 2>&1

echo
echo "  change-pin"
printf '2468\n135790\n' | L "$D" change-pin >/dev/null 2>&1
open_try "$D" 135790; v=$V; [ "$v" = open ] && ok "the changed PIN opens it" || bad "the changed PIN opens it" "got $v"
close_last
open_try "$D" 2468; v=$V; [ "$v" = wrong ] && ok "the one before does not" || bad "the one before does not" "got $v"
close_last
tpm2_dictionarylockout -c >/dev/null 2>&1
[ "$(cryptsetup luksDump "$D" | grep -c 'systemd-tpm2$')" = 1 ] && ok "one sealed PIN, not two (each costs a counted try)" \
    || bad "one sealed PIN" "$(cryptsetup luksDump "$D" | grep -c 'systemd-tpm2$')"

echo
echo "  PCR 7 changes (a dbx or firmware update)"
tpm2_pcrextend 7:sha256=00000000000000000000000000000000000000000000000000000000000000d8 >/dev/null
open_try "$D" 135790; v=$V; [ "$v" = open ] && ok "bound to no PCR: the PIN still opens it, no rescue code" || bad "unbound: still opens" "got $v"
close_last
if [ -z "${THEFT_QUICK:-}" ]; then
    mkdrive "$T/b.img" 1200; D7=$DRV
    "$ENCINIT" "$D7" >/dev/null 2>&1
    L "$D7" encrypt-pass >/dev/null 2>&1 </dev/null
    code7=$(printf '4321\n' | AUROS_LOCK_PCRS=7 L "$D7" set-pin 2>/dev/null | tr -d ' ')
    cryptsetup luksDump "$D7" | grep -q 'tpm2-hash-pcrs:   7' && ok "bound to PCR 7 when asked" || bad "bound to PCR 7 when asked"
    tpm2_pcrextend 7:sha256=00000000000000000000000000000000000000000000000000000000000000db >/dev/null
    open_try "$D7" 4321; v=$V; [ "$v" = changed ] && ok "PCR 7 changed: the page says the computer changed" || bad "PCR 7 changed -> changed" "got $v"
    printf 'pin=4321\nrescue=%s\n' "$code7" > "$T/repin"; mkdir -p /run/auros-lock; cp "$T/repin" /run/auros-lock/repin
    AUROS_LOCK_PCRS=7 L "$D7" repin >/dev/null 2>&1
    open_try "$D7" 4321; v=$V; [ "$v" = open ] && ok "after repin the PIN opens it under the new PCR 7" || bad "repin under the new PCR 7" "got $v"
    close_last
fi

echo
echo "  a different chip (cleared, or another board)"
tpm_stop
tpm_start "$T/tpm2"
open_try "$D" 135790; v=$V; [ "$v" = changed ] && ok "the page says the computer changed" || bad "a new chip -> changed" "got $v"
open_try "$D" "rescue $CODE"; v=$V; [ "$v" = open ] && ok "the rescue code still opens it" || bad "rescue with a new chip" "got $v"
close_last
mkdir -p /run/auros-lock; printf 'pin=1357\nrescue=%s\n' "$CODE" > /run/auros-lock/repin; chmod 600 /run/auros-lock/repin
L "$D" repin >/dev/null 2>&1
open_try "$D" 1357; v=$V; [ "$v" = open ] && ok "repin seals the PIN in the new chip" || bad "repin into the new chip" "got $v"
close_last

echo
echo "  no chip"
mkdrive "$T/c.img" 1200; DN=$DRV
"$ENCINIT" "$DN" >/dev/null 2>&1
L "$DN" encrypt-pass >/dev/null 2>&1
export AUROS_LOCK_TPM="swtpm:path=$T/none" AURLOCK_TPM2_DEVICE="swtpm:path=$T/none"
out=$(printf '1234\n' | L "$DN" set-pin 2>&1)
L "$DN" status | grep -qx state=open-no-pin && ok "a 4-number PIN is refused without a chip" || bad "4 numbers refused without a chip" "$out"
codeN=$(printf '123456\n' | L "$DN" set-pin 2>/dev/null | tr -d ' ')
L "$DN" status | grep -qx pin=pass && ok "6 numbers become a passphrase" || bad "6 numbers become a passphrase"
cryptsetup luksDump "$DN" | grep -q 'argon2id' && ok "made slow to guess (argon2id)" || bad "argon2id"
open_try "$DN" 654321; v=$V; [ "$v" = wrong ] && ok "a wrong PIN says wrong" || bad "no chip: wrong" "got $v"
open_try "$DN" 123456; v=$V; [ "$v" = open ] && ok "the right PIN opens it" || bad "no chip: right" "got $v"
close_last
printf '123456\n246813\n' | L "$DN" change-pin >/dev/null 2>&1
open_try "$DN" 246813; v=$V; [ "$v" = open ] && ok "change-pin without a chip" || bad "change-pin without a chip" "got $v"
close_last
open_try "$DN" "rescue $codeN"; v=$V; [ "$v" = open ] && ok "the rescue code opens it" || bad "no chip: rescue" "got $v"
close_last

echo
echo "  the page (EASY.md)"
"$PIN" --png "$T/p.png" --size 1024 600 --page pin > "$T/geo"
"$PIN" --png "$T/l.png" --size 1024 600 --page locked >> "$T/geo"
"$PIN" --png "$T/r.png" --size 1024 600 --page rescue >> "$T/geo"
small=$(awk '($1=="key" && ($5<44 || $6<44)) || ($1=="link" && ($4<44 || $5<44))' "$T/geo")
[ -z "$small" ] && [ -s "$T/p.png" ] && ok "every key and button is at least 44 px at 1024x600" || bad "targets >= 44 px" "$small"
JARGON=$(sed -n "/^JARGON='/,/'\$/p" "$SRC/tools/plainwords.sh" | tr -d "\n'" | sed 's/^JARGON=//')
words=$(sed -n '/^static const char \*title_of/,/^static int wrap/p' "$SRC/src/aurshell/lockpin.c" \
        | grep -o '"[^"]*"' | grep -iwE "${JARGON:-partition}" || true)
[ -z "$words" ] && ok "no word from EASY.md's list on the page" || bad "no jargon on the page" "$words"

echo
echo "  the EFI partition's start files"
ESPU=ABCD-1234; ROOTU=0b1c2d3e-4f50-6172-8394-a5b6c7d8e9f0; PARTU=80632cff-c8ae-4036-ab78-6e608aa8da9c
stub=$(bash "$LOCKCTL" stub-text "$ESPU" "$ROOTU")
printf '%s\n' "$stub" > "$T/stub.cfg"
# IF NOTHING OF AUROS IS THERE the firmware gets the machine back (as
# build/mkimage's stub does): an else branch that says so and ends in
# `exit`, instead of a bare grub> prompt she cannot use.
printf '%s\n' "$stub" | awk '/^else$/{e=1} e&&/^  sleep 6$/{s=1} e&&s&&/^fi$/{f=1} f&&/^exit$/{x=1} END{exit !x}' \
    && printf '%s\n' "$stub" | grep -q 'AurOS is not on this computer any more' \
    && ok "the stub, when no AurOS is found, says so and exits to the firmware" \
    || bad "the stub exits to the firmware when no AurOS is found" "$stub"
printf '%s\n' "$stub" | grep -A1 'configfile (\$root)/auros/grub.cfg' | grep -q '^  exit$' \
    && ok "a menu that ends hands the machine back to the firmware" || bad "a menu that ends exits" "$stub"
[ "$(printf '%s\n' "$stub" | sed -n '1p')" = "search --no-floppy --fs-uuid --set=root $ESPU" ] \
    && ok "the stub starts from the EFI partition's own menu" || bad "the stub starts from the EFI partition"
if command -v grub-script-check >/dev/null 2>&1; then
    grub-script-check "$T/stub.cfg" >/dev/null 2>&1 && ok "grub accepts the stub's syntax (grub-script-check)" || bad "grub accepts the stub's syntax" "$(grub-script-check "$T/stub.cfg" 2>&1 | head -3)"
else ok "(grub-script-check is not installed here: the stub's syntax is checked on Actions)"; fi

# The real, current menu template, converted for the EFI partition.
M="$T/menu"
mkdir -p "$M/boot/grub" "$M/etc" "$M/usr/lib/auros"
cp "$SRC/rootfs/usr/lib/auros/grub.cfg.in" "$M/usr/lib/auros/"
printf 'UUID=%s  /  ext4  defaults  0 1\nUUID=%s  /boot/efi  vfat  umask=0077  0 2\n' "$ROOTU" "$ESPU" > "$M/etc/fstab"
for v in 6.8.0-9-generic 6.8.0-10-generic; do : > "$M/boot/vmlinuz-$v"; : > "$M/boot/initrd.img-$v"; done
python3 -I "$SRC/rootfs/usr/lib/auros/bootchain" menu --root "$M" --running none >/dev/null 2>&1
CFG="$M/boot/grub/grub.cfg"
if [ -s "$CFG" ] && bash "$LOCKCTL" convert-menu "$CFG" "$T/esp.cfg" "$ESPU" "$ROOTU" "$PARTU"; then
    nl=$(grep -cE '^[[:space:]]*linux[[:space:]]' "$CFG")
    nc=$(grep -cE "^[[:space:]]*linux[[:space:]]+/auros/vmlinuz-.* auros\.lock=PARTUUID=$PARTU( |\$)" "$T/esp.cfg")
    [ "$nl" -ge 4 ] && [ "$nl" = "$nc" ] && ok "the current menu converts: all $nl kernel lines start from the EFI partition and carry the lock" \
        || bad "every kernel line converts" "$nl lines, $nc converted"
    ! grep -v '^[[:space:]]*#' "$T/esp.cfg" | grep -q '/boot/' && grep -q "^search --no-floppy --fs-uuid --set=root $ESPU\$" "$T/esp.cfg" \
        && grep -q 'save_env --file (\$root)/auros/grubenv' "$T/esp.cfg" && ! grep -q '(\$root)/boot/grub/grubenv' "$T/esp.cfg" \
        && ok "its own copy: nothing reads /boot, the environment block is the EFI partition's" || bad "the converted menu reads only the EFI partition" "$(grep -n '/boot/' "$T/esp.cfg" | head -3)"
    if command -v grub-script-check >/dev/null 2>&1; then
        grub-script-check "$T/esp.cfg" >/dev/null 2>&1 && ok "grub accepts the converted menu's syntax (grub-script-check)" || bad "grub accepts the converted menu" "$(grub-script-check "$T/esp.cfg" 2>&1 | head -3)"
    fi
else bad "the current menu converts for the EFI partition" "bootchain or convert-menu refused: $(python3 -I "$SRC/rootfs/usr/lib/auros/bootchain" menu --root "$M" --running none 2>&1 | head -2)"; fi

echo
if [ "$fail" -eq 0 ]; then echo "  $checked checks, all ok"; exit 0; fi
echo "  $fail of $checked checks FAILED"; exit 1
