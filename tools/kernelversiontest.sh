#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

tmp=$(mktemp -d "${TMPDIR:-/tmp}/auros-kernel-version.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM
root="$tmp/rootfs"
mkdir -p "$root/boot" "$root/lib/modules"

fail=0
checked=0
ok() { checked=$((checked + 1)); printf '    %-62s ok\n' "$1"; }
bad() { checked=$((checked + 1)); fail=$((fail + 1)); printf '    %-62s FAIL\n' "$1"; }

# Creation order is unsorted, and lexical `head -1` chooses 10 while
# version order correctly chooses 11. That was the staging/image drift.
for version in 6.8.0-11-generic 6.8.0-10-generic; do
    : > "$root/boot/vmlinuz-$version"
    mkdir -p "$root/lib/modules/$version"
done
old_staging=$(find "$root/lib/modules" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; |
    LC_ALL=C sort | head -1)
old_image=$(find "$root/boot" -name 'vmlinuz-*' -type f -exec basename {} \; |
    sed 's/^vmlinuz-//' | LC_ALL=C sort -V | tail -1)
[ "$old_staging" != "$old_image" ] \
    && ok "two-version fixture reproduces the former selector drift" \
    || bad "two-version fixture reproduces the former selector drift"

got=$(build/kernel-version "$root")
[ "$got" = 6.8.0-11-generic ] \
    && ok "newest version wins independent of directory order" \
    || bad "newest version wins independent of directory order"

rm -rf "$root/lib/modules/6.8.0-11-generic"
if build/kernel-version "$root" >"$tmp/out" 2>"$tmp/err"; then
    bad "newest kernel without matching modules is refused"
elif grep -q 'kernel 6.8.0-11-generic has no matching modules' "$tmp/err"; then
    ok "newest kernel without matching modules is refused"
else
    bad "missing-module refusal explains the exact kernel"
fi

rm -f "$root"/boot/vmlinuz-*
if build/kernel-version "$root" >"$tmp/out" 2>"$tmp/err"; then
    bad "a rootfs without a kernel is refused"
elif grep -q 'no kernel image' "$tmp/err"; then
    ok "a rootfs without a kernel is refused"
else
    bad "missing-kernel refusal is explicit"
fi

# The image now holds TWO kernels: 7.0 (HWE, started) and 6.8 (GA, the fallback).
# Version order, not text order, in every place that must agree on which one
# is "the" kernel: build/kernel-version (staging + image), the menu
# (bootchain) and the hardware facts (gen-compat). Text order puts 6.8 above
# 6.14, and 6.14 above 7.0 only by luck of the first digit.
two="$tmp/two"
mkdir -p "$two/boot" "$two/lib/modules"
for version in 6.8.0-149-generic 7.0.0-42-generic 6.14.0-30-generic; do
    : > "$two/boot/vmlinuz-$version"
    : > "$two/boot/initrd.img-$version"
    mkdir -p "$two/lib/modules/$version"
done
[ "$(build/kernel-version "$two")" = 7.0.0-42-generic ] \
    && ok "7.0 HWE beats 6.8 GA and 6.14 (staging and image)" \
    || bad "7.0 HWE beats 6.8 GA and 6.14 (staging and image)"
[ "$(python3 -I -c 'import importlib.util as u,sys
s=u.spec_from_file_location("g","tools/hw/gen-compat.py"); m=u.module_from_spec(s); s.loader.exec_module(m)
print(m.newest_kernel(sys.argv[1]))' "$two/lib/modules")" = 7.0.0-42-generic ] \
    && ok "the hardware facts are made for the same kernel" \
    || bad "the hardware facts are made for the same kernel"
rm -rf "$two/lib/modules/7.0.0-42-generic" "$two/boot/vmlinuz-7.0.0-42-generic" "$two/boot/initrd.img-7.0.0-42-generic"
[ "$(build/kernel-version "$two")" = 6.14.0-30-generic ] \
    && ok "without 7.0, 6.14 still beats 6.8 (not text order)" \
    || bad "without 7.0, 6.14 still beats 6.8 (not text order)"
: > "$two/boot/vmlinuz-7.0.0-42-generic"; : > "$two/boot/initrd.img-7.0.0-42-generic"
mkdir -p "$two/boot/grub" "$two/etc" "$two/usr/lib/auros"
cp rootfs/usr/lib/auros/grub.cfg.in "$two/usr/lib/auros/"
printf 'UUID=0b1c2d3e-4f50-6172-8394-a5b6c7d8e9f0  /  ext4  defaults  0 1\nUUID=AAAA-BBBB  /boot/efi  vfat  umask=0077  0 2\n' > "$two/etc/fstab"
# bootchain needs python3-cryptography (CI and homebase have it; a bare Mac may not)
if python3 -c 'import cryptography' 2>/dev/null; then
    python3 rootfs/usr/lib/auros/bootchain menu --root "$two" --running none >/dev/null 2>&1 || true
    cfg="$two/boot/grub/grub.cfg"
    { grep -A1 -- '--id auros {' "$cfg" | grep -q 'vmlinuz-7.0.0-42-generic' &&
      grep -A1 -- '--id auros-previous' "$cfg" | grep -q 'vmlinuz-6.14.0-30-generic'; } \
        && ok "the start-up menu starts 7.0 and keeps the next kernel as the fallback" \
        || bad "the start-up menu starts 7.0 and keeps the next kernel as the fallback"
else
    printf '    %-62s skipped (no python3-cryptography here)\n' "the start-up menu starts 7.0 and keeps the next kernel as the fallback"
fi

# The profile names both kernels, and the forge refuses an image that lost one.
profile_kernels=$(sh -c '. ./profiles/desktop.profile; echo " $packages_hardware "' | tr -s ' \n' '  ')
case "$profile_kernels" in *" linux-image-generic-hwe-24.04 "*) hwe=1 ;; *) hwe=0 ;; esac
case "$profile_kernels" in *" linux-image-generic "*) ga=1 ;; *) ga=0 ;; esac
[ "$hwe$ga" = 11 ] \
    && ok "the desktop profile installs the 7.0 HWE kernel and keeps 6.8" \
    || bad "the desktop profile installs the 7.0 HWE kernel and keeps 6.8"
grep -q 'for k in linux-image-generic linux-image-generic-hwe-24.04' build/forge \
    && ok "the forge refuses an image that lost either kernel" \
    || bad "the forge refuses an image that lost either kernel"

refs=$(grep -l 'build/kernel-version' build/staging build/mkimage | wc -l | tr -d ' ')
[ "$refs" = 2 ] \
    && ok "staging and installed image share one kernel selector" \
    || bad "staging and installed image share one kernel selector"

printf '\n%d checks, %d failed\n' "$checked" "$fail"
[ "$fail" -eq 0 ]
