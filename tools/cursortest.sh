#!/bin/sh
# cursortest.sh — can she find the pointer?
#
# The pointer was a 10 x 15 pixel arrow in the theme's colours: the width
# of a letter, on a screen read by someone with presbyopia, and pale on a
# pale wallpaper. DESIGN-DIRECTION §3 (Input): about 32 px, black with a
# white outline on every theme, 48 at 150% text.
#
# Renders the same desktop with and without the pointer (aurshell --mouse)
# and measures what the pointer added: its height, and that it is black
# and white and nothing else.
#
#   AURSHELL=<built aurshell> sh tools/cursortest.sh      (repository root)
set -u
cd "$(dirname "$0")/.." || exit 2
A=${AURSHELL:-}
[ -n "$A" ] && [ -x "$A" ] || { echo "set AURSHELL to a built aurshell"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/cursor.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
mkdir -p "$T/home/.config/auros" "$T/st/c" "$T/apps"
printf 'sandstone\n' > "$T/st/theme"
tail -n +2 themes/templates/shell.conf.tpl | AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates \
    AURORA_STATE="$T/st" AURORA_CACHE="$T/st/c" ./src/aurora/aurora render > "$T/c.conf" 2>/dev/null

shot() { # name scale [mouse args]
    printf '%s\n' "$2" > "$T/home/.config/auros/text-scale"
    HOME="$T/home" AURSHELL_CLOCK=1761206400 AUROS_APP_DIRS="$T/apps" \
        "$A" --conf "$T/c.conf" --shell shells/dock.shell --policy /nonexistent \
        --size 800 500 --png "$T/$1.png" $3 >/dev/null 2>&1
}
measure() { python3 -I - "$T/$1.png" "$T/$2.png" <<'PY'
import sys, zlib, struct
def load(p):
    d = open(p, 'rb').read(); pos = 8; idat = b''; w = h = 0
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos+8]); body = d[pos+8:pos+8+n]; pos += 12 + n
        if t == b'IHDR': w, h = struct.unpack('>II', body[:8])
        if t == b'IDAT': idat += body
    raw = zlib.decompress(idat); bpp = 3; rows = []
    prev = bytearray(w * bpp)
    for y in range(h):
        f = raw[y*(w*bpp+1)]; line = bytearray(raw[y*(w*bpp+1)+1:(y+1)*(w*bpp+1)])
        if f == 1:
            for i in range(bpp, len(line)): line[i] = (line[i] + line[i-bpp]) & 255
        elif f == 2:
            for i in range(len(line)): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(len(line)): line[i] = (line[i] + ((line[i-bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(len(line)):
                a = line[i-bpp] if i >= bpp else 0; b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
                pa, pb, pc = abs(b-c), abs(a-c), abs(a+b-2*c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line); prev = line
    return w, h, rows
w, h, a = load(sys.argv[1]); _, _, b = load(sys.argv[2])
ys = []; cols = set()
for y in range(h):
    for x in range(w):
        if a[y][x*3:x*3+3] != b[y][x*3:x*3+3]:
            ys.append(y); cols.add(bytes(a[y][x*3:x*3+3]))
print(max(ys) - min(ys) + 1 if ys else 0, len(cols), "black-white" if cols <= {b'\x10\x12\x14', b'\xff\xff\xff'} else "other")
PY
}
fail=0
ok() { if [ "$2" = 0 ]; then echo "    ok    $1"; else echo "    FAIL  $1"; fail=1; fi; }
shot none 1.0 ""
shot one 1.0 "--mouse 300 200"
shot none15 1.5 ""
shot big 1.5 "--mouse 300 200"
set -- $(measure one none); h1=$1; kind1=$3
set -- $(measure big none15); h2=$1
[ "$h1" -ge 28 ] && [ "$h1" -le 36 ]; ok "the pointer is about 32 px tall (is $h1)" $?
[ "$kind1" = black-white ];            ok "...black with a white outline, nothing else" $?
[ "$h2" -ge 42 ] && [ "$h2" -le 50 ];  ok "...and about 48 px at 150% text (is $h2)" $?
[ $fail = 0 ] && echo "cursortest: all good" || echo "cursortest: FAILED"
exit $fail
