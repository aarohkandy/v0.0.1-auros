#!/bin/sh
# voicebench.sh — how long does her computer take to understand a sentence, and how
# much memory does it need? Run this ON the old machine (the one the product is for):
# a build server's numbers say little about a 2013 laptop.
#
#   sh tools/voicebench.sh /usr/lib/auros/whisper-cli /usr/share/auros/voice/ggml-base.en-q5_1.bin clip1.wav clip2.wav ...
#
# Clips are 16 kHz mono 16-bit WAVs of somebody saying a wish. One thread, like the
# product's default on a one- or two-core machine (aurvoice uses up to 4). Needs GNU time
# (/usr/bin/time -f). Prints, per clip: seconds of speech, seconds to understand it,
# CPU seconds, peak memory in MB, and the words it heard. The word error rate against what
# was actually said is for you to judge: print the lines and read them.
set -eu
CLI=${1:?usage: voicebench.sh WHISPER_CLI MODEL.bin CLIP.wav ...}
MODEL=${2:?model}
shift 2
[ -x "$CLI" ] || { echo "no recogniser at $CLI"; exit 2; }
[ -f "$MODEL" ] || { echo "no model at $MODEL"; exit 2; }
[ -x /usr/bin/time ] || { echo "needs GNU time at /usr/bin/time"; exit 2; }
printf '%-28s %8s %8s %8s %7s  %s\n' clip speech_s wall_s cpu_s peakMB heard
for wav in "$@"; do
    bytes=$(wc -c < "$wav")
    speech=$(awk "BEGIN { printf \"%.1f\", ($bytes - 44) / 32000 }")
    out=$(/usr/bin/time -f '%e %U %S %M' "$CLI" -m "$MODEL" -f "$wav" -l en -t 1 -nt -np -ng 2> "${TMPDIR:-/tmp}/voicebench.$$") || true
    set -- $(tail -1 "${TMPDIR:-/tmp}/voicebench.$$")
    wall=$1; cpu=$(awk "BEGIN { printf \"%.1f\", $2 + $3 }"); mb=$(awk "BEGIN { printf \"%.0f\", $4 / 1024 }")
    printf '%-28s %8s %8s %8s %7s  %s\n' "$(basename "$wav")" "$speech" "$wall" "$cpu" "$mb" "$(echo $out | cut -c1-70)"
done
rm -f "${TMPDIR:-/tmp}/voicebench.$$"
