#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  grubshelltest — does any grub menu AurOS ships offer a shell?
#
#  A kernel line that starts a shell (init=/bin/bash, single, rescue,
#  emergency, rd.break...) is a root shell for anybody at the keyboard,
#  past her login and her disk's PIN. The menu is locked (superusers),
#  so editing a line asks for a password nobody knows; but a menu ENTRY
#  that starts a shell and is not marked, or a second grub.cfg that was
#  never locked (the ESP stubs, the encrypted layout's copy, the
#  installer's), would undo that. This reads every place AurOS writes
#  one, statically, and refuses a kernel line that gives a shell or an
#  entry titled like one:
#
#    rootfs/usr/lib/auros/grub.cfg.in      the template
#    rootfs/usr/lib/auros/bootchain        the previous-kernel entry it writes
#    build/mkimage                         the EFI partition's stubs
#    rootfs/usr/lib/auros/lock/lockctl     the encrypted layout's stubs and menu
#    src/aurbridge/phases.c                the installer's one-shot menu
#    the menu bootchain actually renders (two kernels, as a machine has)
#    build/mkimage's /etc/default/grub (GRUB_DISABLE_RECOVERY, so Ubuntu's
#    grub-mkconfig, if anything ever ran it, writes none)
#    forge's rootfs, if there is one: update-grub is AurOS's; and any
#    grub.cfg or EFI stub it has (an image's rootfs has them)
#
#  And the scanner itself is shown to see what it is for: lines that do
#  give a shell, in each of the usual spellings, are fed to it first.
#
#    sh tools/grubshelltest.sh [ROOTFS]     (default work/forge/desktop/rootfs, if there)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
RFS="${1:-work/forge/desktop/rootfs}"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-72s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-72s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
command -v python3 >/dev/null || { echo "need python3"; exit 2; }

T=$(mktemp -d "${TMPDIR:-/tmp}/grubshell.XXXXXX"); trap 'rm -rf "$T"' EXIT

cat > "$T/scan.py" <<'EOF'
import re, sys
# What turns a kernel start into a shell, or into a mode that has one.
BAD_TOKEN = re.compile(r"^(single|rescue|emergency|recovery|-s|s|S|1|-b|rd\.break|rd\.break=.*|break|break=.*|rdinit=.*|"
                       r"init=\S*(sh|bash|dash|ash|zsh|ksh|csh|busybox|sulogin|su)|"
                       r"systemd\.unit=(rescue|emergency)(\.target)?|systemd\.debug-shell(=.*)?|"
                       r"systemd\.run=.*)$")
# A grub kernel line starts with the command (in a C string, after a quote).
KERNEL = re.compile(r"""^[\s"']*(linux|linuxefi|linux16|multiboot|multiboot2)\s""")
TITLE = re.compile(r"^\s*(menuentry|submenu)\s+['\"]([^'\"]*)['\"]")
BADTITLE = re.compile(r"recovery|rescue|emergency|single|shell|root console|debug", re.I)

def scan(path_or_text, text=False):
    src = path_or_text if text else open(path_or_text, errors="replace").read()
    bad = []
    for n, line in enumerate(src.splitlines(), 1):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        m = TITLE.match(line)
        if m and BADTITLE.search(m.group(2)):
            bad.append("%d: an entry called \"%s\"" % (n, m.group(2)))
        if KERNEL.search(line):
            for tok in s.split():
                if BAD_TOKEN.match(tok):
                    bad.append("%d: kernel line has %s: %s" % (n, tok, s[:120]))
                    break
    return bad

if __name__ == "__main__":
    if sys.argv[1] == "--text":
        out = scan(sys.stdin.read(), text=True)
    else:
        out = scan(sys.argv[1])
    for b in out: print(b)
    sys.exit(1 if out else 0)
EOF

# ── the scanner sees what it is for ─────────────────────────────────
echo
echo "  the scanner: lines that give a shell are seen, lines that do not are not"
seen() { printf '%s\n' "$1" | python3 -I "$T/scan.py" --text >/dev/null; [ $? = 1 ]; }
unseen() { printf '%s\n' "$1" | python3 -I "$T/scan.py" --text >/dev/null; }
L='    linux  /boot/vmlinuz-6.8.0-1-generic root=UUID=abc ro quiet splash console=tty0'
for bad_opt in 'init=/bin/bash' 'init=/usr/bin/bash' 'init=/bin/sh' 'init=/sbin/sulogin' 'single' 'rescue' \
               'emergency' 'systemd.unit=rescue.target' 'systemd.unit=emergency' 'rd.break' 'rd.break=pre-mount' \
               'rdinit=/bin/sh' '1' 'S' 'systemd.debug-shell=1' 'init=/bin/busybox'; do
    seen "$L $bad_opt" && ok "a kernel line with \"$bad_opt\" is refused" \
                       || bad "a kernel line with \"$bad_opt\" is refused" "the scanner let it through"
done
seen "menuentry 'AurOS (recovery shell)' --class recovery --id auros-recovery {" \
  && ok "an entry titled \"recovery shell\" is refused" || bad "an entry titled \"recovery shell\" is refused"
unseen "$L init_on_alloc=1 slab_nomerge randomize_kstack_offset=on vsyscall=none debugfs=off panic=10 fsck.repair=yes lockdown=integrity" \
  && ok "AurOS's own kernel line is not" || bad "AurOS's own kernel line is not"
unseen "$L init=/usr/lib/auros/start-windows" && ok "...nor start-windows, which sets BootNext and restarts" || bad "...nor start-windows"
unseen "# linux /boot/vmlinuz-x ro single init=/bin/bash   (a comment saying what used to be here)" \
  && ok "a comment is not a kernel line" || bad "a comment is not a kernel line"

# ── what AurOS writes ───────────────────────────────────────────────
echo
echo "  every grub menu and stub AurOS writes"
for f in rootfs/usr/lib/auros/grub.cfg.in rootfs/usr/lib/auros/bootchain build/mkimage \
         rootfs/usr/lib/auros/lock/lockctl src/aurbridge/phases.c; do
    if [ ! -f "$f" ]; then bad "$f is scanned" "no such file (moved? then this test must follow it)"; continue; fi
    out=$(python3 -I "$T/scan.py" "$f") && ok "$f: no shell, no recovery entry" \
                                         || bad "$f: no shell, no recovery entry" "$out"
done

# the menu bootchain really writes, for a machine with two kernels
R="$T/root"; mkdir -p "$R/boot/grub" "$R/etc" "$R/usr/lib/auros"
cp rootfs/usr/lib/auros/grub.cfg.in "$R/usr/lib/auros/"
printf 'UUID=0b1c2d3e-4f50-6172-8394-a5b6c7d8e9f0  /  ext4  defaults  0 1\n' > "$R/etc/fstab"
for v in 6.8.0-9-generic 6.8.0-10-generic; do : > "$R/boot/vmlinuz-$v"; : > "$R/boot/initrd.img-$v"; done
if python3 rootfs/usr/lib/auros/bootchain menu --root "$R" --running none >/dev/null 2>&1 &&
   [ "$(grep -c '^ *linux ' "$R/boot/grub/grub.cfg")" -ge 4 ]; then
    out=$(python3 -I "$T/scan.py" "$R/boot/grub/grub.cfg") && ok "the menu bootchain renders (two kernels): no shell" \
                                                            || bad "the menu bootchain renders (two kernels): no shell" "$out"
else
    bad "the menu bootchain renders (two kernels): no shell" "bootchain would not render the fixture"
fi

# What mkimage puts in /etc/default/grub is what ships (it overwrites
# Ubuntu's): read it where it is written. With GRUB_DISABLE_RECOVERY true,
# Ubuntu's own grub-mkconfig -- which nothing on AurOS runs, update-grub
# being ours -- would still write no recovery entry.
dg=$(sed -n "/cat > \"\$MNT\/etc\/default\/grub\"/,/^EOF/p" build/mkimage)
echo "$dg" | grep -q '^GRUB_DISABLE_RECOVERY="true"$' \
  && ok "build/mkimage: /etc/default/grub says GRUB_DISABLE_RECOVERY=true" \
  || bad "build/mkimage: /etc/default/grub says GRUB_DISABLE_RECOVERY=true" "$(echo "$dg" | head -4)"

# ── a built rootfs ──────────────────────────────────────────────────
#
# THIS IS forge's ROOTFS in CI (work/forge/desktop/rootfs), NOT the disk
# image: it has no /boot/grub/grub.cfg yet (build/mkimage renders it when
# it builds the disk, from the template above) and Ubuntu's own
# /etc/default/grub (mkimage replaces it). A first version of this test
# demanded both and failed a correct build (RC2). What forge's rootfs can
# say: update-grub is AurOS's, so the only thing that writes a menu is
# the template; and a grub.cfg or EFI stub, if there is one (a rootfs
# taken out of a built image has them), has no shell either.
if [ -d "$RFS/etc" ]; then
    echo
    echo "  the built rootfs ($RFS)"
    if [ -f "$RFS/usr/sbin/update-grub" ]; then
        grep -q '/usr/lib/auros/bootchain' "$RFS/usr/sbin/update-grub" \
          && ok "usr/sbin/update-grub is AurOS's (the menu is only ever the template's)" \
          || bad "usr/sbin/update-grub is AurOS's (the menu is only ever the template's)" \
                 "it is Ubuntu's: a kernel update would write a menu with recovery entries" "$(head -3 "$RFS/usr/sbin/update-grub")"
    else
        bad "usr/sbin/update-grub is AurOS's" "no usr/sbin/update-grub in $RFS"
    fi
    n=0
    for f in "$RFS/boot/grub/grub.cfg" "$RFS"/boot/efi/EFI/*/grub.cfg "$RFS"/boot/efi/auros/grub.cfg; do
        [ -f "$f" ] || continue
        n=$((n+1))
        out=$(python3 -I "$T/scan.py" "$f") && ok "${f#$RFS/}: no shell" || bad "${f#$RFS/}: no shell" "$out"
    done
    [ "$n" -gt 0 ] || echo "    (no grub.cfg in it yet: build/mkimage writes the menu when it builds the disk)"
    # A rootfs that has a menu (taken out of a built image) has the
    # image's /etc/default/grub too.
    if [ -f "$RFS/boot/grub/grub.cfg" ] && [ -f "$RFS/etc/default/grub" ]; then
        grep -q '^GRUB_DISABLE_RECOVERY=["]\{0,1\}true' "$RFS/etc/default/grub" \
          && ok "/etc/default/grub (the image's): GRUB_DISABLE_RECOVERY=true" \
          || bad "/etc/default/grub (the image's): GRUB_DISABLE_RECOVERY=true" "$(grep '^GRUB_' "$RFS/etc/default/grub" | head -3)"
    fi
else
    echo
    echo "  (no built rootfs at $RFS; only what is in the tree was read)"
fi

echo
echo "grubshelltest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
