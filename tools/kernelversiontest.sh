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

refs=$(grep -l 'build/kernel-version' build/staging build/mkimage | wc -l | tr -d ' ')
[ "$refs" = 2 ] \
    && ok "staging and installed image share one kernel selector" \
    || bad "staging and installed image share one kernel selector"

printf '\n%d checks, %d failed\n' "$checked" "$fail"
[ "$fail" -eq 0 ]
