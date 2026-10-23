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

# ── the matcher: facts from hw/compat.json, cases from hwcompat.c ────
GEN="$T/gen"
if python3 -I tools/hw/gen-header.py "$GEN" 2>"$T/gen.log"; then
    ok "hw/compat.json becomes the matcher's tables"
else bad "hw/compat.json becomes the matcher's tables" "$(cat "$T/gen.log")"; fi
cat > "$T/cmp_main.c" <<'EOC'
int hwcompat_selftest(void);
int main(void){ return hwcompat_selftest() ? 1 : 0; }
EOC
CMP_INC="-I$SRC -Ihw/lib -I$GEN"
if $CC $WARN -Werror -O2 $CMP_INC -o "$T/cmp" "$T/cmp_main.c" hw/lib/hwcompat.c $SRC/hwinv.c 2>"$T/cc.log" \
   && "$T/cmp" >"$T/cmp.log" 2>&1; then
    ok "hwcompat.c: $(grep -c ' ok$' "$T/cmp.log") cases, no warnings"
else bad "hwcompat.c: its own cases" "$(grep -v ' ok$' "$T/cmp.log" | head -8; head -8 "$T/cc.log")"; fi
if [ -f "$T/cmp.log" ] && grep -q "BCM4352" "$T/cmp.log"; then ok "the BCM4352 case ran"; else bad "the BCM4352 case ran"; fi

# the sample report in the campaign folder is what the renderer prints today
if $CC $WARN $CMP_INC -o "$T/sample" tools/hw/sample.c hw/lib/hwcompat.c $SRC/hwinv.c 2>"$T/cc.log" &&
   "$T/sample" > "$T/sample.txt" && cmp -s "$T/sample.txt" docs/results/hardware/SAMPLE-REPORT.txt; then
    ok "docs/results/hardware/SAMPLE-REPORT.txt is what the report prints"
else bad "docs/results/hardware/SAMPLE-REPORT.txt is what the report prints" \
         "regenerate: see tools/hw/sample.c (cc it with hwcompat.c and hwinv.c, run, save as the sample)"; fi

# every sentence she can read passes the word rules (docs/EASY.md rule 2)
if sh tools/wordlint.sh "$GEN/hwcompat_words.h" >"$T/wl.log" 2>&1; then
    ok "every sentence she reads passes wordlint"
else bad "every sentence she reads passes wordlint" "$(head -8 "$T/wl.log")"; fi

# the committed compat.json is what the committed seeds say (words and rules)
if python3 -I - <<'EOP'
import json
c = json.load(open("hw/compat.json", encoding="utf-8")); s = json.load(open("hw/seeds.json", encoding="utf-8"))
assert c["words"] == s["words"] and c["rules"] == s["rules"], "compat.json is not made from seeds.json: run tools/hw/gen-compat.py"
for r in s["rules"]:
    assert r["says"] in s["words"], r["id"]
EOP
then ok "compat.json carries seeds.json's words and rules"; else bad "compat.json carries seeds.json's words and rules"; fi

# and, where an image is at hand, that it still says what the image can drive
if [ -n "${HW_ROOTFS:-}" ] && [ -d "$HW_ROOTFS" ]; then
    python3 -I tools/hw/gen-compat.py "$HW_ROOTFS" --check | sed 's/^/      /'
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
    hwinv_add_dev(v, "Net", "PCI\\VEN_14E4&DEV_43B1&SUBSYS_0608105B&REV_03", "", "Broadcom 802.11ac", "Broadcom", "x", 0, 71);
    hwinv_add_dev(v, "Camera", "USB\\VID_0BDA&PID_5530&MI_00", "", "Integrated Camera", "", "usbvideo", 0, 0);
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
    if $MW $WARN -Werror -O2 $CMP_INC -c $SRC/hwinv_win.c -o "$T/win.o" 2>"$T/mw.log" &&
       $MW $WARN -Werror -O2 $CMP_INC -c $SRC/hwinv.c -o "$T/inv.o" && $MW $WARN -Werror -O2 $CMP_INC -c hw/lib/hwcompat.c -o "$T/cmp.o" 2>>"$T/mw.log"; then
        ok "hwinv_win.c, hwinv.c and hwcompat.c build clean for Windows"
    else bad "hwinv_win.c, hwinv.c and hwcompat.c build clean for Windows" "$(head -8 "$T/mw.log")"; fi
else
    echo "    (no $MW here: the Windows build is checked on Actions)"
fi

# ── the table of real PCs ────────────────────────────────────────────
if python3 -I tools/hw/results.py --dir tools/hw/fixtures/results --out "$T/res.md" 2>"$T/res.err" &&
   grep -q '^3 machines. Installed and kept working: 2. Refused by the installer: 1 (Lenovo IdeaPad 3 15ABA7: the Windows drive is encrypted' "$T/res.md" &&
   grep -q 'Dell XPS 13 9343 .*wont / no |' "$T/res.md" &&
   grep -q '4 hit, 1 wrong' "$T/res.md" &&
   grep -q 'HP 15-ef.*camera: the report said .works., the real result was .no.' "$T/res.md"; then
    ok "results.py: the table counts refusals, hits and the one promise that failed"
else bad "results.py: the table counts refusals, hits and the one promise that failed" "$(cat "$T/res.err" 2>/dev/null)"; fi
mkdir -p "$T/badres"; printf 'machine: X\ndate: 2026-11-01\ninstall: maybe\nput_windows_back: ok\ndata_loss: no\n' > "$T/badres/x.txt"
if python3 -I tools/hw/results.py --dir "$T/badres" --check >/dev/null 2>&1; then bad "results.py refuses a file with install: maybe"
else ok "results.py refuses a file with install: maybe"; fi
if python3 -I tools/hw/results.py --dir docs/results/hardware --check; then ok "every real-PC result file in docs/results/hardware is valid"
else bad "every real-PC result file in docs/results/hardware is valid"; fi
if python3 -I tools/hw/results.py --dir docs/results/hardware --out "$T/readme.md" && cmp -s "$T/readme.md" docs/results/hardware/README.md; then
    ok "docs/results/hardware/README.md is what the result files give"
else bad "docs/results/hardware/README.md is what the result files give" "run: python3 -I tools/hw/results.py"; fi
sed 's/return "hit" if obs == "yes" else "PROMISED, FAILED"/return "hit"/' tools/hw/results.py > "$T/results_m.py"
if python3 -I "$T/results_m.py" --dir tools/hw/fixtures/results --out "$T/res2.md" 2>/dev/null && grep -q '4 hit, 1 wrong' "$T/res2.md"; then
    bad "mutant: a promise that failed counts as a hit" "the table still says 4 hit, 1 wrong"
else ok "mutant: a promise that failed counts as a hit  -> red"; fi

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
mutant "a USB hub is kept" 's/ \&\& !ci_has(name ? name : "", "hub")//' $SRC/hwinv.c
mutant "JSON stops escaping quotes" "s/if (\*p == '\"' || \*p == '\\\\\\\\')/if (*p == 0x7f)/" $SRC/hwinv.c
mutant "a five-digit id is accepted" 's/if (hexv((unsigned char)p\[n\]) >= 0) continue;/;/' $SRC/hwinv.c
mutant "the printer is paired with the wrong USB device" 's/m \* 100 >= 60 \* shorter/1/' $SRC/hwinv.c

# the matcher: change one line of it, or of the facts, and a case must go red
hmutant() {
    label=$1; expr=$2; file=$3
    sed "$expr" "$file" > "$T/hm.c"
    if cmp -s "$file" "$T/hm.c"; then bad "mutant: $label" "the mutation changed nothing (stale sed)"; return; fi
    rm -rf "$T/hmgen"; mkdir -p "$T/hmgen"; cp "$GEN"/*.h "$T/hmgen/"
    src=hw/lib/hwcompat.c; inv=$SRC/hwinv.c
    case "$file" in
      *hwcompat_data.h) cp "$T/hm.c" "$T/hmgen/hwcompat_data.h" ;;
      *hwinv.c)         inv="$T/hm.c" ;;
      *)                src="$T/hm.c" ;;
    esac
    if $CC $WARN -O2 -I$SRC -I"$T/hmgen" -Ihw/lib -o "$T/hm" "$T/cmp_main.c" "$src" "$inv" 2>"$T/hcc.log"; then
        if "$T/hm" >/dev/null 2>&1; then bad "mutant: $label" "the matcher's cases still passed: they cannot see this"
        else ok "mutant: $label  -> red"; fi
    else bad "mutant: $label" "mutant did not compile: $(head -3 "$T/hcc.log")"; fi
}
hmutant "the Broadcom rule stops matching (wrong id)" 's/"wifi-broadcom-wl-only"/"wifi-broadcom-x"/' "$GEN/hwcompat_data.h"
hmutant "a rule that wants 'no driver' applies to working chips too" 's/if (r->need == 1 \&\& nreal) continue;//' hw/lib/hwcompat.c
hmutant "the best device no longer wins (the worst does)" 's/rank(all\[i\].state) < rank(all\[best\].state)/rank(all[i].state) > rank(all[best].state)/' hw/lib/hwcompat.c
hmutant "Wi-Fi blocks even with a cable socket" 's/if (!cable) { blocks = 1/if (1) { blocks = 1/' hw/lib/hwcompat.c
hmutant "a Wi-Fi card is matched by any module, not a Wi-Fi one" 's/if (cat \&\& m->cat != cat) continue;//' hw/lib/hwcompat.c
hmutant "the journal line forgets that the computer would have no internet" 's/if (v->verdicts\[i\].blocks) blk = 1;/blk = 0;/' $SRC/hwinv.c
hmutant "a missing firmware file is ignored" 's/if (m->fw_unusable) { if (!unusable_module)/if (0) { if (!unusable_module)/' hw/lib/hwcompat.c
hmutant "a printer model matches part of a word" 's/snprintf(m, sizeof m, " %s ", model);/snprintf(m, sizeof m, "%s", model);/' hw/lib/hwcompat.c
hmutant "the Windows subsystem ids are read the wrong way round" 's/uint16_t sv = (uint16_t)(d->subsys \& 0xFFFF), sd = (uint16_t)(d->subsys >> 16);/uint16_t sd = (uint16_t)(d->subsys \& 0xFFFF), sv = (uint16_t)(d->subsys >> 16);/' hw/lib/hwcompat.c

echo
if [ "$fail" -eq 0 ]; then echo "    $checked checks, all as they should be."; else echo "    $fail of $checked wrong."; fi
exit "$fail"
