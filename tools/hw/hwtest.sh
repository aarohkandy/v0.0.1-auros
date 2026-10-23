#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  hwtest -- the hardware inventory and the compatibility matcher.
#
#  Stream HW (docs/handoff/streams/HW.md). No Windows, no image, no
#  network: pure C on the host, plus (when mingw is there) a clean
#  Windows build of the part that cannot run here.
#
#    sh tools/hw/hwtest.sh
#
#  Every check is also shown to FAIL when the thing it checks is broken:
#  the mutants at the end change one line of the real source, rebuild,
#  and the self-test must go red. A test that cannot fail is not a test.
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/../.."
SRC=src/aurbridge
CC="${CC:-cc}"
WARN="-std=gnu11 -Wall -Wextra -Wno-unused-parameter"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-64s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-64s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

T=$(mktemp -d "${TMPDIR:-/tmp}/hwtest.XXXXXX")
trap 'rm -rf "$T"' EXIT

echo
echo "Does the PC inventory read hardware the way Windows writes it?"
echo

# ── the inventory: its own cases, compiled without warnings ──────────
cat > "$T/inv_main.c" <<'EOC'
int hwinv_selftest(void);
int main(void){ return hwinv_selftest() ? 1 : 0; }
EOC
if $CC $WARN -Werror -O2 -I$SRC -o "$T/inv" "$T/inv_main.c" $SRC/hwinv.c 2>"$T/cc.log" \
   && "$T/inv" >"$T/inv.log" 2>&1; then
    ok "hwinv.c: $(grep -c ' ok$' "$T/inv.log") cases, no warnings"
else
    bad "hwinv.c: its own cases" "$(tail -8 "$T/inv.log" "$T/cc.log" 2>/dev/null)"
fi

# ── what it writes is JSON ───────────────────────────────────────────
cat > "$T/dump.c" <<'EOC'
#include "hwinv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    hw_inv *v = hwinv_new();
    strcpy(v->dmi_vendor, "ACME \"Quote\" \\ back\tslash"); strcpy(v->dmi_model, "Ünïcode 9");
    hwinv_add_dev(v, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03", "Broadcom 802.11ac", "Broadcom", "x", 0, 71);
    hwinv_add_dev(v, "Camera", "USB\\VID_0BDA&PID_5530&MI_00", "Integrated Camera", "", "usbvideo", 0, 0);
    hwinv_add_printer(v, "Brother", "Brother HL-L2350DW series", "USB001", 1);
    char *js = hwinv_json(v, "t", "now"); char *tx = hwinv_text(v, "t", "now");
    FILE *f = fopen(argv[1], "wb"); fputs(js, f); fclose(f);
    f = fopen(argv[2], "wb"); fputs(tx, f); fclose(f);
    return 0;
}
EOC
if $CC $WARN -I$SRC -o "$T/dump" "$T/dump.c" $SRC/hwinv.c 2>"$T/cc.log" && "$T/dump" "$T/o.json" "$T/o.txt" \
   && python3 -I - "$T/o.json" <<'EOP'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
assert d["schema"] == 1
assert d["machine"]["vendor"] == 'ACME "Quote" \\ back\tslash', d["machine"]["vendor"]
assert [x["kind"] for x in d["devices"]] == ["wifi", "camera"], d["devices"]
assert d["devices"][0]["vid"] == "14e4" and d["devices"][0]["did"] == "43b1"
assert d["printers"][0]["connection"] == "usb"
EOP
then ok "the JSON parses, keeps quotes, tabs and accents, and has the ids"
else bad "the JSON parses, keeps quotes, tabs and accents, and has the ids" "$(cat "$T/cc.log" 2>/dev/null)"; fi

# ── the Windows part has to at least build clean for Windows ─────────
MW="${MW:-x86_64-w64-mingw32-gcc}"
if command -v "$MW" >/dev/null 2>&1; then
    if $MW $WARN -Werror -O2 -I$SRC -c $SRC/hwinv_win.c -o "$T/win.o" 2>"$T/mw.log" &&
       $MW $WARN -Werror -O2 -I$SRC -c $SRC/hwinv.c -o "$T/inv.o" 2>>"$T/mw.log"; then
        ok "hwinv_win.c and hwinv.c build clean for Windows"
    else bad "hwinv_win.c and hwinv.c build clean for Windows" "$(head -8 "$T/mw.log")"; fi
else
    echo "    (no $MW here: the Windows build is checked on Actions)"
fi

# ── each check must be able to fail ──────────────────────────────────
# mutant "label" 'sed expression' file
mutant() {
    label=$1; expr=$2; file=$3
    cp "$file" "$T/m.c"
    sed "$expr" "$file" > "$T/m.c"
    if cmp -s "$file" "$T/m.c"; then bad "mutant: $label" "the mutation changed nothing (stale sed)"; return; fi
    if $CC $WARN -O2 -I$SRC -o "$T/m" "$T/inv_main.c" "$T/m.c" 2>"$T/mcc.log"; then
        if "$T/m" >/dev/null 2>&1; then bad "mutant: $label" "the self-test still passed: it cannot see this"
        else ok "mutant: $label  -> red"; fi
    else bad "mutant: $label" "mutant did not compile: $(head -3 "$T/mcc.log")"; fi
}
mutant "Wi-Fi interface type 71 is no longer Wi-Fi" 's/if_type == 71/if_type == 7101/' $SRC/hwinv.c
mutant "the PCI device id is read from the wrong field" 's/dev = find_hex(hwid, "DEV_", 4)/dev = find_hex(hwid, "XEV_", 4)/' $SRC/hwinv.c
mutant "a fax queue counts as a printer" 's/"pdf", "fax", /"pdf", /' $SRC/hwinv.c
mutant "a PCI bridge is kept" 's/if (k == HW_K_OTHER \&\& !(problem \&\& real))/if (0)/' $SRC/hwinv.c
mutant "JSON stops escaping quotes" "s/if (\*p == '\"' || \*p == '\\\\\\\\')/if (*p == 0x7f)/" $SRC/hwinv.c
mutant "a five-digit id is accepted" 's/if (hexv((unsigned char)p\[n\]) >= 0) continue;/;/' $SRC/hwinv.c
mutant "the printer is paired with the wrong USB device" 's/m \* 100 >= 60 \* shorter/1/' $SRC/hwinv.c

echo
if [ "$fail" -eq 0 ]; then echo "    $checked checks, all as they should be."; else echo "    $fail of $checked wrong."; fi
exit "$fail"
