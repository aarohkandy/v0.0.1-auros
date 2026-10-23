#!/bin/sh
# tools/dllcanary.sh OUTDIR -- build the DLL-planting test pieces (SEC-003):
#   OUTDIR/canary.dll   writes "<path>.loaded" when anything loads it
#   OUTDIR/control.exe  loads DLLs the unhardened way (the positive control)
# Needs the 64-bit mingw compiler, nothing else. Not shipped.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: tools/dllcanary.sh OUTDIR}
CC=${CC:-x86_64-w64-mingw32-gcc}
mkdir -p "$OUT"
"$CC" -O2 -shared -static -o "$OUT/canary.dll" "$ROOT/tools/dllcanary/canary.c"
"$CC" -O2 -static -municode -o "$OUT/control.exe" "$ROOT/tools/dllcanary/control.c"
ls -l "$OUT/canary.dll" "$OUT/control.exe"
