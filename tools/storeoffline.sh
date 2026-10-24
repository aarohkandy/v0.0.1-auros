#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  storeoffline -- "Get more programs" on a computer with NO network
#
#  The last red step of the real-Windows journey: she pressed "Get more
#  programs" and GNOME Software opened a header bar over an empty page,
#  for five minutes (RC1, RC6: the image carried no Flathub catalogue and
#  could not fetch one). A store that opens blank is where she gives up.
#
#  This runs the image's OWN root file system (read only, under a
#  throw-away overlay), the image's OWN aurshell (headless, the way
#  tools/wltest-apps.sh runs the real programs) and the image's OWN
#  GNOME Software, as the image's own user, in a container with NO
#  network interface at all, and looks at what she would see:
#
#    first open   the page is not empty: GNOME Software logged no
#                 "failed to get featured apps"/"no apps to show", and
#                 the screen carries colour (category tiles, icons)
#    search       typing "vlc" finds VLC: the store's search provider,
#                 asked what the desktop asks, answers org.videolan.VLC,
#                 and the screen shows more than the first one did
#
#      sh tools/storeoffline.sh IMAGE.img [OUTDIR]          must be GREEN on a good image
#      sh tools/storeoffline.sh --expect=empty IMAGE.img    passes only if the store IS empty
#                                                           (proves the test is red on RC6)
#
#  Linux, docker (--privileged: loop devices), sfdisk, python3. ~3 minutes,
#  3 GB of memory. The image is not modified. Write OUTDIR on a disk with
#  room for two screenshots.
# ═══════════════════════════════════════════════════════════════════
set -eu
EXPECT=pass
[ "${1:-}" = --expect=empty ] && { EXPECT=empty; shift; }
IMG=${1:?usage: storeoffline.sh [--expect=empty] IMAGE.img [OUTDIR]}
OUT=${2:-$PWD/storeoffline-out}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"; rm -f "$OUT"/*.png "$OUT"/*.log "$OUT"/search.txt
IMG=$(cd "$(dirname "$IMG")" && pwd)/$(basename "$IMG")

# the root partition (named AUROS-ROOT by build/mkimage), in bytes
read -r OFF SIZE <<EOP
$(sfdisk -J "$IMG" | python3 -I -c '
import json,sys
p=[x for x in json.load(sys.stdin)["partitiontable"]["partitions"] if x.get("name")=="AUROS-ROOT"]
assert p, "no AUROS-ROOT partition"
print(p[0]["start"]*512, p[0]["size"]*512)')
EOP
echo "root partition: offset $OFF, size $SIZE"

run() { # MODE SECONDS
    docker run --rm --privileged --network none --memory 4g --shm-size 1g \
        -v "$IMG":/img.raw:ro -v "$HERE/storeoffline-inner.sh":/inner.sh:ro -v "$OUT":/out \
        -e OFF="$OFF" -e SIZE="$SIZE" -e MODE="$1" -e SECS="$2" ubuntu:24.04 bash /inner.sh
}
echo "== first open"; run first "${FIRST_SECS:-60}"
echo "== typing vlc"; run vlc 90

python3 -I - "$OUT" "$EXPECT" <<'EOPY'
import sys, zlib, struct, os
out, expect = sys.argv[1], sys.argv[2]
def vivid(path):
    """number of vividly coloured pixels in a PNG (category tiles, icons): the empty store has none"""
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    i, idat, w = 8, b'', 0
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i+8]); c = d[i+8:i+8+n]; i += 12 + n
        if t == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', c[:10]); assert bd == 8 and ct in (2, 6), (bd, ct)
        elif t == b'IDAT': idat += c
    bpp = 3 if ct == 2 else 4; raw = zlib.decompress(idat); stride = w * bpp
    prev = bytearray(stride); pos = 0; count = 0
    for _ in range(h):
        f = raw[pos]; row = bytearray(raw[pos+1:pos+1+stride]); pos += 1 + stride
        for x in range(stride):
            a = row[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
            if f == 1: row[x] = (row[x] + a) & 255
            elif f == 2: row[x] = (row[x] + b) & 255
            elif f == 3: row[x] = (row[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                row[x] = (row[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(0, stride, bpp):
            r, g, bl = row[x], row[x+1], row[x+2]; mx, mn = max(r, g, bl), min(r, g, bl)
            if mx > 90 and mx - mn > 0.35 * mx: count += 1
        prev = row
    return count
def text(p):
    try: return open(os.path.join(out, p), errors='replace').read()
    except OSError: return ''
first, vlc = vivid(os.path.join(out, 'first.png')), vivid(os.path.join(out, 'vlc.png'))
gs1, gsv, found = text('gs-first.log'), text('gs-vlc.log'), text('search.txt')
empty_log = 'no apps to show' in gs1
has_vlc = 'org.videolan.VLC' in found
checks = [
    ('first open: GNOME Software did not say "no apps to show"', not empty_log),
    ('first open: the screen carries colour (%d vivid pixels, need 3000)' % first, first >= 3000),
    ('search: the store answers "vlc" with VLC (%s)' % (found.strip()[:70] or 'nothing'), has_vlc),
    ('search: the screen carries colour (%d vivid pixels, need 3000)' % vlc, vlc >= 3000),
]
bad = [n for n, ok in checks if not ok]
for n, ok in checks: print('    %-86s %s' % (n, 'ok' if ok else 'FAIL'))
if expect == 'empty':
    # today's image: the store is empty, and the test must see that
    seen = empty_log and not has_vlc and first < 3000
    print('storeoffline: the store is %s' % ('EMPTY, as expected: the test is red on this image' if seen else 'NOT empty: this image is not the broken one'))
    sys.exit(0 if seen else 1)
print('storeoffline: %s' % ('the store shows programs and finds VLC with no network' if not bad else '%d of %d FAILED' % (len(bad), len(checks))))
sys.exit(1 if bad else 0)
EOPY
