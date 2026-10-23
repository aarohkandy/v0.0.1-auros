#!/bin/sh
# voicetest.sh — aurvoice (src/aurwish/voice.c), against stand-ins.
#
# The microphone, the recogniser, piper, the player and espeak-ng are small
# scripts this test writes, so every line aurvoice prints is a consequence
# of what it was handed, and what it did with her voice can be checked:
# the recogniser is given an anonymous memory file and nothing else; no
# file anywhere gains her words; a cancel transcribes nothing.
#
#   sh tools/voicetest.sh              # from the repository root; needs cc, python3
#   sh tools/voicetest.sh --no-mutants
#
# Then each check is shown to fail: twelve things are broken, one at a time,
# in a copy of voice.c, and the test must go red every time.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/voicetest.$$
trap 'rm -rf "$T"; kill $(jobs -p) 2>/dev/null' EXIT
mkdir -p "$T/bin" "$T/work"

fails=0; checks=0
# Wait for a background job, but never for ever: a broken aurvoice must fail
# this test, not hang it.
finish() { i=0; while kill -0 "$1" 2>/dev/null && [ $i -lt 40 ]; do sleep 0.5; i=$((i + 1)); done
           kill -9 "$1" 2>/dev/null; wait "$1" 2>/dev/null; return 0; }
ok() { checks=$((checks + 1)); if [ "$2" = 0 ]; then printf '  %-72s ok\n' "$1"; else printf '  %-72s FAIL\n' "$1"; fails=$((fails + 1)); fi; }

# ── the stand-ins ──────────────────────────────────────────────────

# A microphone. MODE (a file) says what it hears: tone | quiet | silence |
# short | wav. It stays open, as a real one does, until it is told to stop.
cat > "$T/bin/rec.sh" <<EOF
#!/bin/sh
python3 - "\$(cat "$T/rec.mode")" <<'PY'
import sys, math, struct
mode = sys.argv[1]
rate = 16000
amp = {"tone": 8000, "quiet": 500, "silence": 0, "short": 8000, "wav": 8000}[mode]
secs = 0.2 if mode == "short" else 2.0
pcm = b"".join(struct.pack("<h", int(amp * math.sin(2 * math.pi * 220 * i / rate))) for i in range(int(rate * secs)))
out = sys.stdout.buffer
if mode == "wav":
    out.write(b"RIFF" + struct.pack("<I", 0xFFFFFFFF) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
              + b"LIST" + struct.pack("<I", 4) + b"INFO" + b"data" + struct.pack("<I", 0xFFFFFFFF))
out.write(pcm); out.flush()
PY
exec sleep 60
EOF

# The recogniser. It insists on being handed an anonymous memory file
# holding a 16 kHz mono 16-bit WAV, and says what it was given.
cat > "$T/bin/whisper.sh" <<EOF
#!/bin/sh
touch "$T/whisper.ran"
printf '%s\n' "\$@" > "$T/whisper.args"
f=""; prev=""
for a in "\$@"; do [ "\$prev" = "-f" ] && f="\$a"; prev="\$a"; done
mode=\$(cat "$T/whisper.mode")
case "\$f" in /proc/self/fd/*) ;; *) echo "not an fd path: \$f" > "$T/whisper.proof"; exit 3;; esac
target=\$(readlink "\$f")
python3 - "\$f" "\$target" > "$T/whisper.proof" <<'PY'
import sys, struct
f, target = sys.argv[1], sys.argv[2]
d = open(f, "rb").read()
ok = d[:4] == b"RIFF" and d[8:12] == b"WAVE" and struct.unpack("<HHIIHH", d[20:36]) == (1, 1, 16000, 32000, 2, 16)
n = struct.unpack("<I", d[40:44])[0]
ok = ok and n == len(d) - 44 and n > 0
print("wav=%s samples=%d anonymous=%s" % ("ok" if ok else "bad", n // 2, "yes" if target.startswith("/memfd:aurvoice") else "NO: " + target))
PY
case "\$mode" in
  ok)    echo " hello there, make the words bigger";;
  blank) echo " [BLANK_AUDIO]";;
  ghost) echo " Thank you.";;
  long)  python3 -c 'import sys; sys.stdout.write(" " + "café\x07 \x1b[2J" * 120 + "\n")';;
  fail)  exit 1;;
  hang)  exec sleep 300;;
esac
EOF

cat > "$T/bin/piper.sh" <<EOF
#!/bin/sh
cat > "$T/piper.stdin"
printf '%s\n' "\$@" > "$T/piper.args"
head -c 4000 /dev/zero
EOF
cat > "$T/bin/player.sh" <<EOF
#!/bin/sh
printf '%s\n' "\$@" > "$T/player.args"
wc -c > "$T/player.bytes"
EOF
cat > "$T/bin/espeak.sh" <<EOF
#!/bin/sh
printf '%s\n' "\$@" > "$T/espeak.args"
EOF
chmod +x "$T/bin/"*.sh
echo '{ "audio": { "sample_rate": 16000 }, "espeak": { "voice": "en-us" } }' > "$T/voice.onnx.json"
: > "$T/voice.onnx"

build() {   # $1 = voice.c, $2 = out, $3 = recorder macro value
    cc -O1 -g -std=gnu11 -w -o "$2" "$1" \
       -DAURVOICE_WHISPER="\"$T/bin/whisper.sh\"" -DAURVOICE_MODEL="\"$T/model.bin\"" \
       -DAURVOICE_PIPER="\"$T/bin/piper.sh\"" -DAURVOICE_VOICE="\"$T/voice.onnx\"" \
       -DAURVOICE_ESPEAK="\"$T/bin/espeak.sh\"" -DAURVOICE_WHISPER_MS=2500 \
       -DAURVOICE_RECORDERS="$3" \
       -DAURVOICE_PLAYERS="{ \"$T/bin/player.sh\", \"--raw\", \"--rate=%RATE%\", NULL }"
}
REC_OK="{ \"$T/bin/rec.sh\", NULL }"
REC_NONE='{ "/nonexistent/recorder", NULL }'

run_all() {   # $1 = binary under test; prints nothing, sets fails/checks
    A=$1
    cd "$ROOT"

    # listen: she presses I've finished.
    echo tone > "$T/rec.mode"; echo ok > "$T/whisper.mode"; rm -f "$T/whisper.ran"
    before=$(find "$T" -type f ! -name 'whisper.*' ! -name 'rec.mode' ! -name 'out.*' ! -name 'err.*' | sort | tr '\n' ' ')
    "$A" listen --max 30 > "$T/out.1" 2> "$T/err.1" & pid=$!
    sleep 1.5
    # A process that is not dumpable has its /proc entries owned by root.
    owner=$(stat -c %u /proc/$pid/environ 2>/dev/null)
    kill -USR1 $pid; finish $pid
    out=$(cat "$T/out.1")
    [ "$out" = "heard hello there, make the words bigger" ]; ok "I've finished: her words come back as one 'heard' line" $?
    grep -q 'wav=ok samples=32000 anonymous=yes' "$T/whisper.proof" 2>/dev/null
    ok "the recogniser got a WAV of exactly what was said, in an anonymous memory file" $?
    after=$(find "$T" -type f ! -name 'whisper.*' ! -name 'rec.mode' ! -name 'out.*' ! -name 'err.*' | sort | tr '\n' ' ')
    [ "$before" = "$after" ]; ok "no file anywhere gained a copy of her voice" $?
    if [ "$(id -u)" = 0 ]; then echo "  (running as root: the no-core-dump check is skipped)"
    else [ "$owner" = 0 ]; ok "no core dump can hold her voice while it is recording (not dumpable)" $?; fi
    grep -qx -- '-l' "$T/whisper.args" && grep -qx 'en' "$T/whisper.args" && ! grep -qiE 'http|://|--server' "$T/whisper.args"
    ok "the recogniser is asked for English and given no address" $?

    # listen: the time limit is hers, and it is kept.
    rm -f "$T/whisper.ran"
    s=$(date +%s)
    "$A" listen --max 2 > "$T/out.2" 2>&1
    e=$(date +%s)
    [ "$(cat "$T/out.2")" = "heard hello there, make the words bigger" ] && [ $((e - s)) -le 6 ]
    ok "--max 2 stops by itself and still transcribes" $?

    # listen: she cancels.
    rm -f "$T/whisper.ran"
    "$A" listen --max 30 > "$T/out.3" 2>&1 & pid=$!
    sleep 1.5; kill -TERM $pid; i=0; while kill -0 $pid 2>/dev/null && [ $i -lt 20 ]; do sleep 0.5; i=$((i+1)); done
    wait $pid; rc=$?
    [ ! -s "$T/out.3" ] && [ "$rc" = 1 ] && [ ! -e "$T/whisper.ran" ]
    ok "cancel: nothing printed, nothing transcribed, exit 1" $?

    # listen: nothing to record with.
    "$B_NOMIC" listen --max 5 > "$T/out.4" 2>&1
    [ "$(cat "$T/out.4")" = "nomic" ]; ok "no microphone it can use: 'nomic'" $?

    # listen: what she said is not usable.
    echo silence > "$T/rec.mode"; rm -f "$T/whisper.ran"
    "$A" listen --max 2 > "$T/out.5" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.5")" = "unsure" ] && [ ! -e "$T/whisper.ran" ]
    ok "a silent recording is 'unsure' and is not sent to the recogniser" $?
    echo short > "$T/rec.mode"; rm -f "$T/whisper.ran"
    "$A" listen --max 2 > "$T/out.6" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.6")" = "unsure" ] && [ ! -e "$T/whisper.ran" ]
    ok "a fifth of a second is not a sentence: 'unsure'" $?
    echo tone > "$T/rec.mode"; echo blank > "$T/whisper.mode"
    "$A" listen --max 2 > "$T/out.7" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.7")" = "unsure" ]; ok "[BLANK_AUDIO] is 'unsure', not words" $?
    echo quiet > "$T/rec.mode"; echo ghost > "$T/whisper.mode"
    "$A" listen --max 2 > "$T/out.8" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.8")" = "unsure" ]; ok "'Thank you.' out of a near-silent room is 'unsure'" $?
    echo tone > "$T/rec.mode"
    "$A" listen --max 2 > "$T/out.9" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.9")" = "heard Thank you." ]; ok "'Thank you.' said out loud is believed" $?

    # listen: a recorder that hands over a WAV.
    echo wav > "$T/rec.mode"; echo ok > "$T/whisper.mode"
    "$A" listen --max 2 > "$T/out.10" 2>&1 & pid=$!; sleep 1.5; kill -USR1 $pid; finish $pid
    grep -q 'wav=ok samples=32000' "$T/whisper.proof"; ok "a recorder's own WAV header is not heard as sound" $?

    # listen: the words are cleaned.
    echo tone > "$T/rec.mode"; echo long > "$T/whisper.mode"
    "$A" listen --max 2 > "$T/out.11" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    python3 - "$T/out.11" <<'PY'
import sys
b = open(sys.argv[1], "rb").read()
line = b.rstrip(b"\n")
ok = line.startswith(b"heard ") and len(line) - 6 <= 300 and b"\n" not in line
t = line[6:].decode("utf-8")           # valid UTF-8, never half a character
ok = ok and not any(ord(c) < 32 or ord(c) == 127 for c in t) and "[2J" not in t
sys.exit(0 if ok else 1)
PY
    ok "long, odd output: one clean line, at most 300 bytes, whole characters" $?

    # listen: the recogniser breaks or hangs.
    echo fail > "$T/whisper.mode"
    "$A" listen --max 2 > "$T/out.12" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    [ "$(cat "$T/out.12")" = "broken" ]; ok "a recogniser that fails: 'broken'" $?
    echo hang > "$T/whisper.mode"
    s=$(date +%s)
    "$A" listen --max 2 > "$T/out.13" 2>&1 & pid=$!; sleep 1.2; kill -USR1 $pid; finish $pid
    e=$(date +%s)
    [ "$(cat "$T/out.13")" = "broken" ] && [ $((e - s)) -le 8 ]; ok "a recogniser that hangs is given up on: 'broken'" $?

    # say, with piper.
    rm -f "$T/piper.stdin" "$T/player.args" "$T/player.bytes" "$T/pwned"
    "$A" say "Hello, \$(touch $T/pwned) \`touch $T/pwned\` and
 a second line" ; rc=$?
    [ "$rc" = 0 ] && [ ! -e "$T/pwned" ]; ok "say: the words are words; nothing in them is run" $?
    [ "$(cat "$T/piper.stdin")" = "Hello, \$(touch $T/pwned) \`touch $T/pwned\` and a second line" ]
    ok "say: piper gets them on stdin, newlines made spaces" $?
    grep -qx -- '--rate=16000' "$T/player.args" && [ "$(tr -d ' \n' < "$T/player.bytes")" = 4000 ]
    ok "say: the player is given the voice's own sample rate and what piper made" $?

    # say, with espeak-ng (no piper).
    rm -f "$T/espeak.args"
    "$B_NOPIPER" say "-rm is not a flag" ;
    [ "$(sed -n 1,4p "$T/espeak.args" | tr '\n' ' ')" = "-s 140 -- -rm is not a flag " ]
    ok "say, without piper: espeak-ng is slow, and a leading dash is a word" $?
    long=$(python3 -c 'print("word " * 200)')
    "$B_NOPIPER" say "$long"
    [ "$(tail -1 "$T/espeak.args" | wc -c)" -le 302 ]; ok "say: no more than 300 bytes are spoken" $?
}

echo "aurvoice"
build "$ROOT/src/aurwish/voice.c" "$T/real" "$REC_OK" || { echo "does not build"; exit 1; }
build "$ROOT/src/aurwish/voice.c" "$T/real-nomic" "$REC_NONE"
# espeak-only: a binary whose piper is not there
cc -O1 -std=gnu11 -w -o "$T/real-nopiper" "$ROOT/src/aurwish/voice.c" \
   -DAURVOICE_PIPER="\"$T/bin/none\"" -DAURVOICE_ESPEAK="\"$T/bin/espeak.sh\"" \
   -DAURVOICE_RECORDERS="$REC_NONE" -DAURVOICE_PLAYERS='{ "/nonexistent/player", NULL }'
B_NOMIC="$T/real-nomic"; B_NOPIPER="$T/real-nopiper"
run_all "$T/real"
echo
echo "$checks checks, $fails failed"
[ "$fails" = 0 ] || exit 1
[ "${1:-}" = "--no-mutants" ] && exit 0

# Each mutant: what it breaks ^ text to find ^ text to put in its place.
cat > "$T/mutants.txt" <<'EOF'
recordings under 0.4 s are sent on^if (lv.samples < MIN_SAMPLES || lv.peak < 300) {^if (lv.peak < 300) {
silence is sent on^if (lv.samples < MIN_SAMPLES || lv.peak < 300) {^if (lv.samples < MIN_SAMPLES) {
cancel kills it without letting go of her voice^sa.sa_handler = on_cancel; sigaction(SIGTERM, &sa, NULL);^
her voice can be dumped^prctl(PR_SET_DUMPABLE, 0);^
[BLANK_AUDIO] is believed^if (c == '[' || c == '(') { skip = 1; close_ch = c == '[' ? ']' : ')'; continue; }^
ghost words are believed^(lv.msq < 400.0 * 400.0 && ghost_words(words))^(0)
a recorder's WAV header is heard as sound^if (h && h <= b->n) {^if (0) {
control characters are kept^if (c < 0x20 || c == 0x7f) c = ' ';\n        if (c == ' ') { if (!space) out[o++] = ' '; space = 1; continue; }\n        out[o++] = (char)c;\n        space = 0;\n    }\n    while (o > 0 && out[o - 1] == ' ') o--;\n    /* Not through^if (0) c = ' ';\n        if (c == ' ') { if (!space) out[o++] = ' '; space = 1; continue; }\n        out[o++] = (char)c;\n        space = 0;\n    }\n    while (o > 0 && out[o - 1] == ' ') o--;\n    /* Not through
a leading dash is a flag for espeak^"-s", "140", "--", t, NULL^"-s", "140", t, NULL
the recogniser is not given a memory file^syscall(SYS_memfd_create, "aurvoice", 0u)^syscall(SYS_memfd_create, "", 0u)
a hung recogniser is waited for ever^if (now_ms() - t0 > WHISPER_MS) break;^
the sentence is not capped for the voice^for (; *in && o + 1 < outn && o < TEXT_MAX; in++) {^for (; *in && o + 1 < outn; in++) {
EOF
n=0; bad=0
while IFS='^' read -r what edits_find edits_repl; do
    [ -n "$what" ] || continue
    n=$((n + 1))
    M="$T/m$n"; mkdir -p "$M"
    cp "$ROOT/src/aurwish/voice.c" "$M/voice.c"
    if ! FILE="$M/voice.c" FIND="$edits_find" REPL="$edits_repl" python3 - <<'PY'
import os, sys
s = open(os.environ["FILE"]).read()
find = os.environ["FIND"].replace("\\n", "\n").replace("\\|", "|")
repl = os.environ["REPL"].replace("\\n", "\n").replace("\\|", "|")
if s.count(find) != 1:
    sys.stderr.write("   find text occurs %d times\n" % s.count(find)); sys.exit(3)
open(os.environ["FILE"], "w").write(s.replace(find, repl))
PY
    then echo "  BAD MUTANT $n ($what): it does not apply"; bad=$((bad + 1)); continue; fi
    if ! build "$M/voice.c" "$M/real" "$REC_OK" 2>"$M/cc.err"; then echo "  BAD MUTANT $n ($what): it does not compile"; head -3 "$M/cc.err"; bad=$((bad + 1)); continue; fi
    build "$M/voice.c" "$M/real-nomic" "$REC_NONE" 2>/dev/null
    cc -O1 -std=gnu11 -w -o "$M/real-nopiper" "$M/voice.c" -DAURVOICE_PIPER="\"$T/bin/none\"" \
       -DAURVOICE_ESPEAK="\"$T/bin/espeak.sh\"" -DAURVOICE_RECORDERS="$REC_NONE" \
       -DAURVOICE_PLAYERS='{ "/nonexistent/player", NULL }' 2>/dev/null
    B_NOMIC="$M/real-nomic"; B_NOPIPER="$M/real-nopiper"
    f0=$fails; c0=$checks
    run_all "$M/real" > "$M/log" 2>&1
    if [ "$fails" -gt "$f0" ]; then echo "  killed    $n: $what"; else echo "  SURVIVED  $n: $what"; bad=$((bad + 1)); fi
    fails=$f0; checks=$c0
    rm -rf "$M"
done < "$T/mutants.txt"
echo
if [ "$bad" = 0 ]; then echo "all $n mutants killed"; else echo "$bad of $n mutants SURVIVED or were bad"; exit 1; fi
