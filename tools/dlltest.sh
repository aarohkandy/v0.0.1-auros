#!/bin/sh
# tools/dlltest.sh -- SEC-003 under Wine: a DLL planted beside the
# installer is never loaded. windows.yml runs the same test on real Windows
# (the step "a DLL planted beside the installer is never loaded").
#
#   1. a "canary" DLL (tools/dllcanary/canary.c) is copied beside the
#      installer under the name of every DLL it must not take from there;
#   2. control.exe loads those names the unhardened way; the ones it
#      finds are the ones this test can speak for (Wine lets a few of its
#      own built-in DLLs win, Windows does not: windows.yml requires all);
#   3. both installer programs run through everything that loads a DLL
#      (preflight, the report, the window, the page tests) with the
#      canaries beside them and with them as the current folder;
#   4. not one canary may have been loaded.
#
# Needs the build in out/ and wine. Exit 0 = pass.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
OUT=${OUT:-out}
[ -f "$OUT/aurbridge.exe" ] && [ -f "$OUT/aurbridge-wizard.exe" ] || { echo "build first: sh build/aurbridge"; exit 1; }
sh tools/dllcanary.sh "$OUT/canary" >/dev/null || exit 1
NAMES="winhttp.dll cfgmgr32.dll oleacc.dll comctl32.dll wlanapi.dll iphlpapi.dll winspool.drv wtsapi32.dll dwmapi.dll uxtheme.dll version.dll"
T=${TMPDIR:-/tmp}; [ -d /dev/shm ] && T=/dev/shm
D="$T/dlltest.$$"; rm -rf "$D"; mkdir -p "$D"
cp "$OUT/aurbridge.exe" "$OUT/aurbridge-wizard.exe" "$OUT/canary/control.exe" "$D/"
for n in $NAMES; do cp "$OUT/canary/canary.dll" "$D/$n"; done
cd "$D" || exit 1
W="WINEDEBUG=-all"
fail=0

mkdir -p "$D/r1" "$D/r2" "$D/r3"
# the windowed runs first: Wine settles on "no screen" for a prefix whose
# first program ran without one (tools/wizard/check.sh keeps this order too)
env $W timeout 300 xvfb-run -a wine aurbridge-wizard.exe --navtest "Z:$D/r2" >/dev/null 2>&1
env $W timeout 200 xvfb-run -a wine aurbridge-wizard.exe --selftest "Z:$D/r3" >/dev/null 2>&1
env $W timeout 120 wine aurbridge.exe selftest >/dev/null 2>&1
env $W timeout 120 wine aurbridge.exe preflight >/dev/null 2>&1
env $W timeout 120 wine aurbridge.exe report "Z:$D/r1" >/dev/null 2>&1
env $W timeout 100 wine aurbridge-wizard.exe --report "Z:$D/r1" >/dev/null 2>&1
# A download, to a port nobody listens on: WinHTTP is loaded for it (from
# System32), and the answer is "could not reach", not "would not let AurOS
# use the internet", which is what a WinHTTP that failed to load says.
Z64=0000000000000000000000000000000000000000000000000000000000000000
printf 'base http://127.0.0.1:9/\npiece p.bin 1000 %s\n' "$Z64" > pieces.txt
# (into a folder of its own that does not exist yet: since SEC-010 the installer
# makes the download folder private, and moves aside one it did not make)
env $W timeout 100 wine aurbridge.exe getimage pieces.txt "Z:$D/dl/x.img" "$Z64" 1000 > fetch.log 2>&1
grep -q 'could not reach' fetch.log \
    || { echo "FAIL: the download did not get as far as the network (WinHTTP did not load)"; fail=1; }
grep -q "screen reader    : \"" "$D/r3/selftest.log" 2>/dev/null \
    || { echo "FAIL: the window test did not get as far as the screen-reader line (oleacc never ran)"; fail=1; }

# what the installer loaded, written down BEFORE the control runs
LOADED=""; for n in $NAMES; do [ -f "$n.loaded" ] && LOADED="$LOADED $n"; done
rm -f ./*.loaded

# the control, last (a windowless first run makes Wine give up on the screen)
env $W timeout 60 wine control.exe $NAMES >/dev/null 2>&1
SPEAK=""; for n in $NAMES; do [ -f "$n.loaded" ] && SPEAK="$SPEAK $n"; done
echo "control loads the planted copy of:$SPEAK"
[ -n "$SPEAK" ] || { echo "FAIL: the control loaded none, so this test proves nothing"; rm -rf "$D"; exit 1; }

for n in $SPEAK; do
    case " $LOADED " in *" $n "*) echo "FAIL: the installer loaded the planted $n"; fail=1 ;; esac
done
[ $fail = 0 ] && echo "DLL TEST: PASS ($(echo $SPEAK | wc -w) planted names, none loaded)" || echo "DLL TEST: FAIL"
rm -rf "$D"
exit $fail
