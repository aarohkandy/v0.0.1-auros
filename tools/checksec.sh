#!/bin/sh
# checksec.sh -- did the hardening in build/cflags reach the binaries?
#
#   sh tools/checksec.sh FILE...      every check on every file; exit 1 if any fails
#   sh tools/checksec.sh --selftest   build a program with and without the
#                                     flags and prove each check tells them apart
#
# ELF (readelf):
#   pie      ELF type DYN                      -fPIE -pie / -static-pie
#   relro    a GNU_RELRO segment               -z relro
#   now      BIND_NOW (full RELRO)             -z now
#   nx       GNU_STACK not executable          (the default; -z execstack breaks it)
#   cet      IBT and SHSTK in .note.gnu.property  -fcf-protection=full
#   ssp      imports __stack_chk_fail          -fstack-protector-strong
#   fortify  imports a __*_chk function        -D_FORTIFY_SOURCE=3 (needs -O2)
# ssp and fortify are read from the dynamic symbols, so they are checked
# on dynamically linked programs only: a static binary carries libc's own
# copies of those functions whether our code was built with the flags or
# not, so their presence there proves nothing.
# -fstack-clash-protection leaves no mark in a binary; build/cflags is
# its only witness.
#
# PE (the Windows installer): DllCharacteristics must have DYNAMIC_BASE
# (ASLR), HIGH_ENTROPY_VA (64-bit ASLR) and NX_COMPAT (DEP).
set -u

fails=0
bad()  { printf '  FAIL %-8s %s\n' "$1" "$2"; fails=$((fails + 1)); }
good() { [ -n "${CHECKSEC_QUIET:-}" ] || printf '  ok   %-8s %s\n' "$1" "$2"; }

pe_flags() {   # print the DllCharacteristics bits we require that are missing
    python3 - "$1" <<'EOF'
import struct, sys
b = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", b, 0x3C)[0]
assert b[pe:pe + 4] == b"PE\0\0"
opt = pe + 24
dllc = struct.unpack_from("<H", b, opt + 70)[0]    # same offset in PE32 and PE32+
want = {"DYNAMIC_BASE": 0x40, "HIGH_ENTROPY_VA": 0x20, "NX_COMPAT": 0x100}
print(" ".join(k for k, v in want.items() if not dllc & v))
EOF
}

check_pe() {
    missing=$(pe_flags "$1") || { bad pe "$1: not a PE file this can read"; return; }
    if [ -z "$missing" ]; then good aslr+dep "$1"; else bad aslr+dep "$1: missing $missing"; fi
}

check_elf() {
    f=$1
    hdr=$(readelf -hW "$f" 2>/dev/null) || { bad elf "$f: not an ELF file"; return; }
    seg=$(readelf -lW "$f" 2>/dev/null)
    dyn=$(readelf -dW "$f" 2>/dev/null)
    note=$(readelf -nW "$f" 2>/dev/null)
    case "$hdr" in *"Type:"*"DYN "*) good pie "$f" ;; *) bad pie "$f: not position-independent" ;; esac
    case "$seg" in *GNU_RELRO*) good relro "$f" ;; *) bad relro "$f: no GNU_RELRO segment" ;; esac
    case "$dyn" in *BIND_NOW*|*"Flags: NOW"*|*" NOW "*) good now "$f" ;;
                   *) bad now "$f: lazy binding (no -z now)" ;; esac
    stack=$(printf '%s\n' "$seg" | grep GNU_STACK || true)
    case "$stack" in *RWE*) bad nx "$f: executable stack" ;; "") bad nx "$f: no GNU_STACK" ;;
                     *) good nx "$f" ;; esac
    case "$note" in *IBT*SHSTK*|*SHSTK*IBT*) good cet "$f" ;;
                    *) bad cet "$f: no IBT+SHSTK property" ;; esac
    if printf '%s\n' "$seg" | grep -q INTERP; then
        syms=$(readelf --dyn-syms -W "$f" 2>/dev/null)
        case "$syms" in *__stack_chk_fail*) good ssp "$f" ;; *) bad ssp "$f: no stack protector" ;; esac
        if printf '%s\n' "$syms" | grep -qE ' __[a-z_0-9]+_chk(@|$)'; then good fortify "$f"
        else bad fortify "$f: no _FORTIFY_SOURCE calls"; fi
    else
        good static "$f (ssp/fortify not checkable in a static binary)"
    fi
}

check() {
    case "$(head -c 2 "$1" 2>/dev/null)" in
        MZ) check_pe "$1" ;;
        *)  check_elf "$1" ;;
    esac
}

selftest() {
    HERE=$(cd "$(dirname "$0")" && pwd)
    # shellcheck source=build/cflags
    . "$HERE/../build/cflags"
    T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
    cat > "$T/p.c" <<'EOF'
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    char buf[64];
    strcpy(buf, argv[argc - 1]);       /* unknown length: __strcpy_chk */
    puts(buf);
    snprintf(buf, sizeof buf, "%d %s", argc, argv[0]);
    puts(buf);
    return 0;
}
EOF
    WEAK="-O2 -U_FORTIFY_SOURCE -fno-stack-protector -fcf-protection=none -no-pie -Wl,-z,lazy -Wl,-z,norelro -Wl,-z,execstack"
    WEAK_STATIC="-O2 -U_FORTIFY_SOURCE -fno-stack-protector -fcf-protection=none -no-pie -static -Wl,-z,lazy -Wl,-z,norelro -Wl,-z,execstack"
    CC=${CC:-cc}
    st=0
    # shellcheck disable=SC2086
    { $CC $HARDEN_CFLAGS -o "$T/good" "$T/p.c" && $CC $WEAK -o "$T/weak" "$T/p.c" &&
      $CC $HARDEN_STATIC -o "$T/good-static" "$T/p.c" && $CC $WEAK_STATIC -o "$T/weak-static" "$T/p.c"; } 2>"$T/cc.log" \
        || { cat "$T/cc.log"; echo "FAIL: could not build the test program"; return 1; }
    expect() {   # file, number of checks that must fail
        out=$(CHECKSEC_QUIET=1 sh "$0" "$1"); n=$(printf '%s\n' "$out" | grep -c '^  FAIL' || true)
        if [ "$n" -eq "$2" ]; then printf 'ok   %-12s %d failing check(s), as it must\n' "$(basename "$1")" "$n"
        else printf 'FAIL %-12s %d failing check(s), expected %d\n%s\n' "$(basename "$1")" "$n" "$2" "$out"; st=1; fi
    }
    expect "$T/good" 0
    expect "$T/weak" 7          # every one of the seven ELF checks
    expect "$T/good-static" 0
    expect "$T/weak-static" 5   # pie relro now nx cet; ssp/fortify are not checkable
    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        # shellcheck disable=SC2086
        x86_64-w64-mingw32-gcc -O2 $HARDEN_PE -o "$T/good.exe" "$T/p.c" &&
        x86_64-w64-mingw32-gcc -O2 -Wl,--disable-dynamicbase,--disable-nxcompat,--disable-high-entropy-va \
            -o "$T/weak.exe" "$T/p.c" || { echo "FAIL: mingw build"; return 1; }
        expect "$T/good.exe" 0
        expect "$T/weak.exe" 1
    else
        echo "skip PE: no x86_64-w64-mingw32-gcc"
    fi
    [ $st -eq 0 ] && echo "PASS: every check fails on a binary built without its flag"
    return $st
}

[ $# -gt 0 ] || { sed -n '2,6p' "$0"; exit 2; }
if [ "$1" = --selftest ]; then selftest; exit $?; fi
for f in "$@"; do check "$f"; done
[ $fails -eq 0 ] || { echo "FAIL: $fails hardening check(s) failed"; exit 1; }
[ -n "${CHECKSEC_QUIET:-}" ] || echo "PASS: $# binar$( [ $# -eq 1 ] && echo y || echo ies) hardened"
exit 0
