#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  channelcheck — is the PUBLISHED update channel what computers trust?
#  (docs/security/UPDATES.md)
#
#  Against the real published repository, in a throwaway ubuntu:24.04
#  container with network:
#    1. real apt, the image's source and the COMMITTED keyring: update
#       is clean, auros has a candidate, every part is at one version,
#       and the packages download and unpack (dpkg-deb) -- no install,
#       the container is not an AurOS image.
#    2. the same index, copied, its Release re-signed with a throwaway
#       key, served locally: apt refuses it (NO_PUBKEY) and fetches no
#       package.
#    3. one byte of Packages changed after signing: apt refuses it.
#  Exit 0 all passed, 1 a check failed, 2 it could not run.
#
#    docker run --rm -e AUROS_REPO=owner/name -v "$PWD":/w:ro -w /w ubuntu:24.04 bash tools/channelcheck.sh
#  (AUROS_REPO because the container has no git remote to derive it from.)
# ═══════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C DEBIAN_FRONTEND=noninteractive
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KR=$ROOT/rootfs/usr/share/keyrings/auros-archive-keyring.asc
PROFILE=desktop
case "$#:${1:-}" in
    0:) ;;
    2:--profile) PROFILE=$2 ;;
    *) echo 'usage: channelcheck.sh [--profile desktop|revive|multilingual]' >&2; exit 2 ;;
esac
AC=(bash "$ROOT/build/aptchannel" --profile "$PROFILE")
"${AC[@]}" descriptor >/dev/null || exit 2
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
need() { echo "channelcheck: $*" >&2; exit 2; }
[ -f /.dockerenv ] || [ -f /run/.containerenv ] || need "refusing to run outside a container"
[ "$(id -u)" = 0 ] || need "run as root"
for i in 1 2 3; do
    timeout 300 apt-get update -q >/dev/null 2>&1 && timeout 300 apt-get install -y -q --no-install-recommends \
        ca-certificates curl gnupg gpgv python3 xz-utils >/tmp/deps.log 2>&1 && break; sleep 10
done
command -v gpg >/dev/null && command -v curl >/dev/null || need "could not install gnupg and curl"
[ -s "$KR" ] || need "no committed keyring at $KR"
URL=$("${AC[@]}" url) || need "no address"

# apt, pointed at one source only, with the keyring as the image has it.
mkdir -p /usr/share/keyrings /etc/apt/preferences.d
cp "$KR" /usr/share/keyrings/auros-archive-keyring.asc
"${AC[@]}" preferences > /etc/apt/preferences.d/auros
only() { # URI  -> apt with just the AurOS source at URI, empty lists
    local T=$1 uri=$2
    rm -rf "$T"; mkdir -p "$T/parts" "$T/lists/partial" "$T/cache/archives/partial"
    "${AC[@]}" source "$uri" > "$T/parts/auros.sources"
    O=(-o Dir::Etc::sourcelist=/dev/null -o Dir::Etc::sourceparts="$T/parts"
       -o Dir::State::lists="$T/lists" -o Dir::Cache="$T/cache" -o APT::Sandbox::User=root)
    A=(apt-get -q "${O[@]}"); C=(apt-cache "${O[@]}")
}

echo
echo "== 1. the published channel, $URL"
only /tmp/real "$URL"
"${A[@]}" update > /tmp/real.log 2>&1
! grep -qE '^(W|E):' /tmp/real.log && grep -q 'InRelease' /tmp/real.log &&
    ok "apt update: clean, signed by the committed key" || bad "apt update: clean, signed by the committed key" "$(sed 's/^/  | /' /tmp/real.log)"
"${C[@]}" policy auros > /tmp/pol.log 2>&1
cand=$(sed -n 's/^ *Candidate: //p' /tmp/pol.log | head -1)
[ -n "$cand" ] && [ "$cand" != '(none)' ] && ok "auros has a candidate: $cand" || bad "auros has a candidate" "$(cat /tmp/pol.log)"
mkdir -p /tmp/dl && cd /tmp/dl && rm -f ./*.deb
"${A[@]}" download auros auros-desktop auros-first auros-bootchain auros-ferry auros-config auros-archive-keyring > /tmp/dl.log 2>&1
n=$(ls ./*.deb 2>/dev/null | wc -l)
[ "$n" -ge 6 ] && ok "the packages download through apt, verified ($n .debs)" || bad "the packages download through apt, verified" "$n .debs" "$(tail -5 /tmp/dl.log)"
seen_profile=
for d in ./*.deb; do
    identity=$("${AC[@]}" package-profile "$d") || { bad "downloaded package profile: $d"; continue; }
    [ -z "$seen_profile" ] || [ "$seen_profile" = "$identity" ] || bad "mixed legacy and identified packages"
    seen_profile=$identity
done
vs=$(for d in ./*.deb; do dpkg-deb -f "$d" Version; done | sort -u | wc -l)
[ "$vs" = 1 ] && ok "every part is at one version" || bad "every part is at one version" "$(for d in ./*.deb; do echo "$d $(dpkg-deb -f "$d" Version)"; done)"
cmp -s <(dpkg-deb --fsys-tarfile ./auros-archive-keyring_*.deb | tar -xO ./usr/share/keyrings/auros-archive-keyring.asc) "$KR" &&
    ok "auros-archive-keyring carries the committed key" || bad "auros-archive-keyring carries the committed key"
cd /

echo
echo "== 2. the same index, re-signed with another key"
rm -rf /tmp/evil && mkdir -p /tmp/evil/srv
for f in Packages Packages.gz Release InRelease Release.gpg; do curl -fsSL --retry 3 -o "/tmp/evil/srv/$f" "$URL$f" || need "could not fetch $f"; done
for f in $(sed -n 's/^Filename: \.\///p' /tmp/evil/srv/Packages); do curl -fsSL --retry 3 -o "/tmp/evil/srv/$f" "$URL$f" || need "could not fetch $f"; done
"${AC[@]}" verify /tmp/evil/srv "$KR" >/tmp/profile-verify.log 2>&1 &&
    ok "signed channel and package identities match $PROFILE" || bad "signed channel and package identities match $PROFILE" "$(cat /tmp/profile-verify.log)"
export GNUPGHOME=/tmp/evil/g; mkdir -m 700 "$GNUPGHOME"
gpg --batch --quiet --passphrase '' --quick-gen-key 'not AurOS' ed25519 sign 1d 2>/dev/null
gpg --batch --yes --quiet --digest-algo SHA512 -u 'not AurOS' --clearsign -o /tmp/evil/srv/InRelease /tmp/evil/srv/Release
gpg --batch --yes --quiet --digest-algo SHA512 -u 'not AurOS' --armor --detach-sign -o /tmp/evil/srv/Release.gpg /tmp/evil/srv/Release
unset GNUPGHOME
python3 -m http.server 8977 --bind 127.0.0.1 --directory /tmp/evil/srv >/dev/null 2>&1 & hp=$!
sleep 1
only /tmp/e2 "http://127.0.0.1:8977/"
"${A[@]}" update > /tmp/e2.log 2>&1
grep -q NO_PUBKEY /tmp/e2.log && ok "refused: not signed by the committed key" || bad "refused: not signed by the committed key" "$(sed 's/^/  | /' /tmp/e2.log)"
[ -z "$(ls /tmp/e2/lists 2>/dev/null | grep -i Packages)" ] && ok "  ... and no package list was taken" || bad "  ... and no package list was taken" "$(ls /tmp/e2/lists)"

echo
echo "== 3. one byte of Packages changed after signing"
rm -rf /tmp/e3 && cp -r /tmp/evil/srv /tmp/e3srv
for f in InRelease Release Release.gpg Packages Packages.gz; do curl -fsSL --retry 3 -o "/tmp/e3srv/$f" "$URL$f" || need "could not fetch $f"; done
# (through temporary files: a pipe into the file it reads empties it)
sed '0,/^Description:/s/^Description:/Description: x/' /tmp/e3srv/Packages > /tmp/e3srv/P.tmp && mv /tmp/e3srv/P.tmp /tmp/e3srv/Packages
gzip -9n -c /tmp/e3srv/Packages > /tmp/e3srv/P.gz && mv /tmp/e3srv/P.gz /tmp/e3srv/Packages.gz
! cmp -s /tmp/e3srv/Packages <(curl -fsSL "${URL}Packages") || need "the tamper changed nothing"
kill $hp 2>/dev/null
python3 -m http.server 8978 --bind 127.0.0.1 --directory /tmp/e3srv >/dev/null 2>&1 & hp=$!
sleep 1
only /tmp/e3 "http://127.0.0.1:8978/"
"${A[@]}" update > /tmp/e3.log 2>&1
grep -qiE 'Hash Sum mismatch|hashes' /tmp/e3.log && ok "refused: Packages does not match the signed Release" ||
    bad "refused: Packages does not match the signed Release" "$(sed 's/^/  | /' /tmp/e3.log)"
kill $hp 2>/dev/null

echo
if [ "$fail" -eq 0 ]; then echo "channelcheck: $checked checks, all passed"; exit 0; fi
echo "channelcheck: $fail of $checked checks FAILED"; exit 1
