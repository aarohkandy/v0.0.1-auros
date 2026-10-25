#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  updatetest — AurOS's update channel, proven with a throwaway key
#  (docs/security/UPDATES.md)
#
#  Real packages made by build/aptchannel, a real signed repository made
#  by build/aptchannel, served over HTTP, read by the real apt with the
#  image's own apt policy (rootfs/etc/apt), installed by the real
#  unattended-upgrade. The keys are made here and die with the run.
#
#    pack     the packages say what they must (files, conffiles, exact-
#             version metapackage), a file the build changed stays out,
#             the same tree packs to the same bytes, the repository
#             refuses a package that is not AurOS's, and `verify` (the
#             publish workflow's gate) refuses a wrong key and a changed
#             Packages.
#    live     a signed update installs by itself; unsigned, signed by
#             another key the computer trusts, a changed .deb, a changed
#             Packages and an older version are refused; a package that
#             is not AurOS's, sealed with AurOS's own key, is refused; a
#             key rotation goes through; a setting edited on the computer
#             neither stops the update nor is overwritten.
#    rollback the update keeps the version before it; when the desktop
#             fails in its last safe mode, shell-guard puts it back and
#             pins the broken version out; the daily update does not
#             bring it back; a newer fix still installs.
#
#  Every protection can be removed, to watch the test catch it:
#    --break=trusted    the source says Trusted: yes (no signature check)
#    --break=signedby   no Signed-By: any key the computer trusts will do
#    --break=pin        no preferences.d/auros: the repository may replace
#                       any package
#    --break=origin     no 53auros-updates: nothing installs by itself
#    --break=keep       no keep-previous hook: a broken update cannot be
#                       undone
#  Each must make this exit 1.
#
#    sudo bash tools/updatetest.sh            (in a throwaway ubuntu:24.04)
#    sudo bash tools/updatetest.sh --break=pin
#
#  It rewrites /etc/apt and installs and removes packages, so it refuses
#  to run outside a container. Needs: gpg gpgv dpkg-deb apt-get
#  unattended-upgrade python3, and the `hello` package installed (the
#  stand-in for "a package that is Ubuntu's"). Exit 0 all passed, 1 a
#  check failed, 2 it could not run.
# ═══════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C DEBIAN_FRONTEND=noninteractive
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AC="$ROOT/build/aptchannel"
BREAK=""
# --publisher-only runs package/publisher negatives without installing updates.
PUBLISHER_ONLY=0
for a in "$@"; do
    case "$a" in
        --publisher-only) PUBLISHER_ONLY=1 ;;
        --break=trusted|--break=signedby|--break=pin|--break=origin|--break=keep) BREAK=${a#--break=} ;;
        *) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s FAIL\n' "$1"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
need() { echo "updatetest: $*" >&2; exit 2; }

[ "$(id -u)" = 0 ] || need "run as root (it installs packages)"
[ -f /.dockerenv ] || [ -f /run/.containerenv ] || [ -n "${UPDATETEST_NOT_A_CONTAINER_I_KNOW:-}" ] ||
    need "refusing to run outside a container: it rewrites /etc/apt"
for t in gpg gpgv dpkg-deb apt-get unattended-upgrade python3; do
    command -v $t >/dev/null || need "needs $t"
done
dpkg-query -W -f='${Status}' hello 2>/dev/null | grep -q 'ok installed' ||
    need "needs the hello package installed (apt-get install hello)"
HELLO=$(dpkg-query -W -f='${Version}' hello)

T=$(mktemp -d /tmp/updatetest.XXXXXX)
SRV="$T/srv"; mkdir -p "$SRV" "$T/aside-d" "$T/aside-list" "$T/log"
HTTP_PID=""
cleanup() {
    [ -n "$HTTP_PID" ] && kill "$HTTP_PID" 2>/dev/null
    # Ubuntu's sources back, so the container is as it was found.
    rm -f /etc/apt/sources.list.d/auros.sources
    mv "$T"/aside-d/* /etc/apt/sources.list.d/ 2>/dev/null
    mv "$T"/aside-list/sources.list /etc/apt/ 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

# ── keys: AurOS's, another vendor's, and AurOS's next one ──────────
export GNUPGHOME="$T/gnupg"; mkdir -m 700 "$GNUPGHOME"
keygen() {
    gpg --batch --quiet --passphrase '' --quick-gen-key "$1" ed25519 sign 1d 2>/dev/null || return 1
    gpg --list-keys --with-colons "=$1" 2>/dev/null | awk -F: '/^fpr:/{print $10; exit}'
}
A=$(keygen 'AurOS updates (test)')          || need "gpg could not make a key"
B=$(keygen 'Another vendor (test)')         || need "gpg could not make a key"
C=$(keygen 'AurOS updates, next key (test)') || need "gpg could not make a key"
gpg --armor --export "$A" > "$T/A.asc"
gpg --armor --export "$B" > "$T/B.asc"
gpg --armor --export "$A" "$C" > "$T/AC.asc"   # one block, both keys: a rotation

# ── packages, from a small repository of AurOS's shape ─────────────
# build/aptchannel packs only files byte-identical to the repository's
# copy, so it runs from a copy of itself next to a tiny rootfs/.
F="$T/src"; mkdir -p "$F/build" "$F/tools" "$F/rootfs/etc/auros"
cp "$AC" "$F/build/aptchannel"; cp "$ROOT/tools/repo.sh" "$F/tools/"
export SOURCE_DATE_EPOCH=1767225600
V0=0.1.0.20260101000000; V1=0.1.0.20260102000000; V2=0.1.0.20260103000000; V3=0.1.0.20260104000000
mkrfs() { # DIR SAYS SETTING
    rm -rf "$1"; mkdir -p "$1/usr/bin" "$1/etc/auros"
    printf '#!/bin/sh\necho %s\n' "$2" > "$1/usr/bin/aurshell"; chmod 0755 "$1/usr/bin/aurshell"
    printf 'setting=%s\n' "$3" > "$1/etc/auros/updatetest.conf"
    cp "$1/etc/auros/updatetest.conf" "$F/rootfs/etc/auros/updatetest.conf"
    # A file this build changed: the repository says one thing, the
    # image another. It must stay the image's.
    printf 'repository\n' > "$F/rootfs/etc/auros/changed.conf"
    printf 'this build\n' > "$1/etc/auros/changed.conf"
}
pack() { # LABEL SAYS SETTING VERSION
    mkrfs "$T/rfs-$1" "$2" "$3"
    bash "$F/build/aptchannel" pack "$T/rfs-$1" "$T/debs-$1" "$4" > "$T/log/pack-$1" 2>&1 ||
        { cat "$T/log/pack-$1"; need "aptchannel pack failed"; }
}
pack v0   zero 1 $V0
pack v1   one  1 $V1
pack v2   two  2 $V2
pack evil evil 2 $V2      # what an attacker would like to be v2
pack v2b  two  2 $V2      # v2 again: must be the same bytes
pack v3   three 2 $V3     # the fix after a broken v2

# The stand-in for one of Ubuntu's packages, at a version that would
# win, as somebody holding AurOS's key could publish it.
mkdir -p "$T/hello99/DEBIAN" "$T/hello99/usr/share/doc/hello"
printf 'Package: hello\nVersion: 99\nArchitecture: amd64\nMaintainer: nobody <nobody@invalid>\nDescription: not from Ubuntu\n not from Ubuntu\n' \
    > "$T/hello99/DEBIAN/control"
echo 'replaced' > "$T/hello99/usr/share/doc/hello/REPLACED"
dpkg-deb --root-owner-group --build "$T/hello99" "$T/hello_99_amd64.deb" >/dev/null || need "dpkg-deb failed"

echo
echo "== pack: what the packages carry"
d1="$T/debs-v1"
dpkg-deb -c "$d1/auros-desktop_${V1}_amd64.deb" | grep -q ' \./usr/bin/aurshell$' &&
    ok "auros-desktop carries /usr/bin/aurshell" || bad "auros-desktop carries /usr/bin/aurshell"
dpkg-deb -I "$d1/auros-config_${V1}_amd64.deb" conffiles 2>/dev/null | grep -qx '/etc/auros/updatetest.conf' &&
    ok "auros-config marks /etc files as conffiles" || bad "auros-config marks /etc files as conffiles"
dpkg-deb -c "$d1/auros-config_${V1}_amd64.deb" | grep -q 'changed\.conf' &&
    bad "a file this build changed stays the image's" "it was packaged" ||
    ok "a file this build changed stays the image's"
deps=$(dpkg-deb -f "$d1/auros_${V1}_amd64.deb" Depends)
case "$deps" in *"auros-desktop (= $V1)"*"auros-config (= $V1)"*|*"auros-config (= $V1)"*"auros-desktop (= $V1)"*)
    ok "auros holds every part at exactly its own version" ;;
    *) bad "auros holds every part at exactly its own version" "Depends: $deps" ;; esac
same=1
for f in "$T"/debs-v2/*.deb; do cmp -s "$f" "$T/debs-v2b/${f##*/}" || same=0; done
[ "$same" = 1 ] && ok "the same tree packs to the same bytes" || bad "the same tree packs to the same bytes"

mkdir -p "$T/foreign"; cp "$d1"/*.deb "$T/hello_99_amd64.deb" "$T/foreign/"
bash "$AC" repo "$T/foreign" "$T/foreign-repo" >/dev/null 2>&1 &&
    bad "the repository refuses a package that is not AurOS's" ||
    ok "the repository refuses a package that is not AurOS's"

# ── repositories ────────────────────────────────────────────────────
repo() { # NAME DEBDIR [KEY...]   (no key: unsigned)
    local n=$1 dd=$2; shift 2
    bash "$AC" repo "$dd" "$T/r-$n" > "$T/log/repo-$n" 2>&1 || { cat "$T/log/repo-$n"; need "aptchannel repo failed"; }
    if [ $# -gt 0 ]; then
        bash "$AC" sign "$T/r-$n" "$@" >> "$T/log/repo-$n" 2>&1 || { cat "$T/log/repo-$n"; need "aptchannel sign failed"; }
    fi
}
repo good    "$T/debs-v2" "$A"
repo unsigned "$T/debs-v2"
repo vendor  "$T/debs-v2" "$B"
repo old     "$T/debs-v0" "$A"
repo newkey  "$T/debs-v2" "$C"
repo both    "$T/debs-v2" "$A" "$C"
repo fixed   "$T/debs-v3" "$A"
# A .deb changed after it was signed for.
repo baddeb  "$T/debs-v2" "$A"
cp "$T/debs-evil/auros-desktop_${V2}_amd64.deb" "$T/r-baddeb/"
# Packages changed after it was signed for, to name the attacker's .deb.
repo badidx  "$T/debs-v2" "$A"
cp "$T/debs-evil/auros-desktop_${V2}_amd64.deb" "$T/r-badidx/evil.deb"
python3 - "$T/r-badidx" <<'EOF'
import hashlib, os, sys
d = sys.argv[1]
ev = open(os.path.join(d, "evil.deb"), "rb").read()
out, cur = [], None
for line in open(os.path.join(d, "Packages")).read().split("\n"):
    if line.startswith("Package: "): cur = line[9:]
    if cur == "auros-desktop":
        if line.startswith("Filename: "): line = "Filename: ./evil.deb"
        elif line.startswith("Size: "): line = "Size: %d" % len(ev)
        elif line.startswith("SHA256: "): line = "SHA256: " + hashlib.sha256(ev).hexdigest()
        elif line.startswith("SHA512: "): line = "SHA512: " + hashlib.sha512(ev).hexdigest()
    out.append(line)
open(os.path.join(d, "Packages"), "w").write("\n".join(out))
EOF
gzip -9n -c "$T/r-badidx/Packages" > "$T/r-badidx/Packages.gz"
# AurOS's own key, used to slip in somebody else's package: the index
# is re-made and signed properly, so only the pin can stop it.
repo foreign "$T/debs-v2" "$A"
cp "$T/hello_99_amd64.deb" "$T/r-foreign/"
{ dpkg-deb -f "$T/r-foreign/hello_99_amd64.deb"
  printf 'Filename: ./hello_99_amd64.deb\nSize: %s\nSHA256: %s\n\n' \
      "$(stat -c %s "$T/r-foreign/hello_99_amd64.deb")" \
      "$(sha256sum "$T/r-foreign/hello_99_amd64.deb" | cut -d' ' -f1)"; } >> "$T/r-foreign/Packages"
bash "$AC" release "$T/r-foreign" >/dev/null && bash "$AC" sign "$T/r-foreign" "$A" >/dev/null ||
    need "could not re-make the foreign repository"

echo
echo "== verify: the publish workflow's own gate"
bash "$AC" verify "$T/r-good" "$T/A.asc" >/dev/null 2>&1 &&
    ok "a repository signed by the pinned key passes" || bad "a repository signed by the pinned key passes"
bash "$AC" verify "$T/r-vendor" "$T/A.asc" >/dev/null 2>&1 &&
    bad "signed by another key: refused" || ok "signed by another key: refused"
bash "$AC" verify "$T/r-badidx" "$T/A.asc" >/dev/null 2>&1 &&
    bad "Packages changed after signing: refused" || ok "Packages changed after signing: refused"
bash "$AC" verify "$T/r-baddeb" "$T/A.asc" >/dev/null 2>&1 &&
    bad "a .deb changed after signing: refused" || ok "a .deb changed after signing: refused"

# ── the computer: AurOS's apt policy, a key it trusts for something
#    else, AurOS's key pinned by Signed-By ──────────────────────────
PORT=$((8100 + RANDOM % 800))
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$SRV" > "$T/log/http" 2>&1 &
HTTP_PID=$!
for _ in $(seq 50); do
    (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break; sleep 0.1
done
URL="http://127.0.0.1:$PORT/"

# Dormant personal channels: package/publisher identity and actual apt policy.
for profile in desktop revive multilingual; do
    mkrfs "$T/rfs-$profile" "$profile" 1
    bash "$F/build/aptchannel" --profile "$profile" pack "$T/rfs-$profile" "$T/debs-$profile" "$V2" >"$T/log/pack-$profile" 2>&1 || need "profile pack failed"
    bash "$AC" --profile "$profile" repo "$T/debs-$profile" "$T/profile-$profile" >/dev/null || need "profile repo failed"
    bash "$AC" --profile "$profile" sign "$T/profile-$profile" "$A" >/dev/null || need "profile sign failed"
    bash "$AC" --profile "$profile" verify "$T/profile-$profile" "$T/A.asc" >/dev/null 2>&1 && ok "$profile: own signed repository accepted" || bad "$profile: own signed repository accepted"
    bash "$AC" --profile "$profile" preferences > "$T/prefs-$profile"
    for other in desktop revive multilingual; do
        [ "$other" = "$profile" ] && continue
        bash "$AC" --profile "$other" verify "$T/profile-$profile" "$T/A.asc" >/dev/null 2>&1 && bad "$other refuses signed $profile repository" || ok "$other refuses signed $profile repository"
        bash "$AC" --profile "$other" repo "$T/debs-$profile" "$T/refused" >/dev/null 2>&1 && bad "$other refuses $profile packages" || ok "$other refuses $profile packages"
    done
done
# Legacy desktop is accepted only as a coherent set, never mixed with
# newly identified packages. Missing metadata is not a non-desktop escape.
mkdir -p "$T/debs-legacy" "$T/debs-mixed"
for deb in "$T/debs-desktop"/*.deb; do
    rm -rf "$T/unpack-profile"
    dpkg-deb -R "$deb" "$T/unpack-profile" >/dev/null
    sed -i '/^X-AurOS-Profile:/d' "$T/unpack-profile/DEBIAN/control"
    dpkg-deb --root-owner-group -b "$T/unpack-profile" "$T/debs-legacy/${deb##*/}" >/dev/null || need "legacy fixture failed"
done
bash "$AC" repo "$T/debs-legacy" "$T/profile-legacy" >/dev/null &&
    bash "$AC" sign "$T/profile-legacy" "$A" >/dev/null &&
    bash "$AC" verify "$T/profile-legacy" "$T/A.asc" >/dev/null 2>&1 && ok "legacy desktop remains compatible" || bad "legacy desktop remains compatible"
cp "$T/debs-legacy"/*.deb "$T/debs-mixed/"
cp "$T/debs-desktop/auros_${V2}_amd64.deb" "$T/debs-mixed/"
bash "$AC" repo "$T/debs-mixed" "$T/refused" >/dev/null 2>&1 && bad "mixed legacy/identified refused" || ok "mixed legacy/identified refused"
for profile in revive multilingual; do
    bash "$AC" --profile "$profile" repo "$T/debs-legacy" "$T/refused" >/dev/null 2>&1 && bad "$profile refuses legacy packages" || ok "$profile refuses legacy packages"
done
# Re-sign mutations with the trusted key: validate identities, not just GPG.
for mutation in suite index-profile compressed-index; do
    cp -a "$T/profile-revive" "$T/mutation-$mutation"
    if [ "$mutation" = suite ]; then
        sed -i 's/^Suite: .*/Suite: stable/' "$T/mutation-$mutation/Release"
    elif [ "$mutation" = compressed-index ]; then
        printf 'not the plain index\n' | gzip -n > "$T/mutation-$mutation/Packages.gz"
        python3 - "$T/mutation-$mutation" <<'PYFIX'
import hashlib, pathlib, sys
root = pathlib.Path(sys.argv[1])
release = root / "Release"
lines = release.read_text().splitlines()
algorithm = None
for i, line in enumerate(lines):
    if line in ("SHA256:", "SHA512:"):
        algorithm = line[:-1].lower()
    elif line.startswith(" ") and line.split()[-1] == "Packages.gz":
        data = (root / "Packages.gz").read_bytes()
        lines[i] = f" {hashlib.new(algorithm, data).hexdigest()} {len(data)} Packages.gz"
release.write_text("\n".join(lines) + "\n")
PYFIX
    else
        sed -i 's/^X-AurOS-Profile: revive/X-AurOS-Profile: multilingual/' "$T/mutation-$mutation/Packages"
        bash "$AC" --profile revive release "$T/mutation-$mutation" >/dev/null || need "mutation release failed"
    fi
    bash "$AC" --profile revive sign "$T/mutation-$mutation" "$A" >/dev/null || need "mutation signing failed"
    bash "$AC" --profile revive verify "$T/mutation-$mutation" "$T/A.asc" >/dev/null 2>&1 && bad "signed $mutation mutation refused" || ok "signed $mutation mutation refused"
done
# A signature authenticates only the named hashes. Both index files must be
# bound to it exactly once; a valid package's own hashes cannot fill that gap.
for mutation in missing-plain missing-gzip missing-both missing-stanza duplicate-record duplicate-stanza duplicate-stanza-space duplicate-stanza-case unindented-record traversal extra-record bad-size bad-hash extra-token; do
    cp -a "$T/profile-revive" "$T/hash-$mutation"
    python3 - "$T/hash-$mutation/Release" "$mutation" <<'PYHASH'
from pathlib import Path
import sys
path = Path(sys.argv[1]); mutation = sys.argv[2]
lines = path.read_text().splitlines()
start = lines.index("SHA256:")
end = start + 1
while end < len(lines) and lines[end].startswith(" "):
    end += 1
records = lines[start+1:end]
assert len(records) == 2
if mutation.startswith("missing-"):
    if mutation == "missing-stanza":
        lines[start:end] = []
    else:
        remove = {"missing-plain": {"Packages"}, "missing-gzip": {"Packages.gz"}, "missing-both": {"Packages", "Packages.gz"}}[mutation]
        lines[start+1:end] = [r for r in records if r.split()[-1] not in remove]
elif mutation == "duplicate-record":
    lines[start+1:end] = records + [records[0]]
elif mutation.startswith("duplicate-stanza"):
    header = {"duplicate-stanza": "SHA256:", "duplicate-stanza-space": "SHA256: ", "duplicate-stanza-case": "sha256:"}[mutation]
    lines[start:end] = ["SHA256:"] + records + [header] + records
elif mutation == "unindented-record":
    lines.insert(end, records[0].lstrip())
else:
    parts = records[0].split()
    if mutation == "traversal": parts[2] = "../Packages"
    elif mutation == "extra-record":
        lines.insert(end, " " + " ".join(parts[:2] + ["Other"]))
    elif mutation == "bad-size": parts[1] = "-1"
    elif mutation == "bad-hash": parts[0] = "g" * 64
    elif mutation == "extra-token": parts.append("unexpected")
    if mutation != "extra-record": lines[start+1] = " " + " ".join(parts)
path.write_text("\n".join(lines) + "\n")
PYHASH
    bash "$AC" --profile revive sign "$T/hash-$mutation" "$A" >/dev/null || need "hash mutation signing failed"
    bash "$AC" --profile revive verify "$T/hash-$mutation" "$T/A.asc" >/dev/null 2>&1 && bad "signed $mutation hash records refused" || ok "signed $mutation hash records refused"
done

for profile in revive multilingual; do
    bash "$AC" --profile "$profile" image "$T/rfs-$profile" "$profile" 1 >/dev/null 2>"$T/log/image-guard" && bad "$profile image activation refused" || {
        grep -q 'profile image channels remain disabled' "$T/log/image-guard" && ok "$profile image activation refused" || bad "$profile image activation refused for intended reason"
    }
done
for invalid in office school-kiosk '' '../desktop'; do
    bash "$AC" --profile "$invalid" descriptor >/dev/null 2>&1 && bad "unavailable profile '$invalid' refused" || ok "unavailable profile '$invalid' refused"
done
if [ "$PUBLISHER_ONLY" = 1 ]; then
    echo "updatetest publisher: $checked checks, $fail failed"
    [ "$fail" = 0 ]; exit $?
fi
# One trusted signing key is intentionally shared: only label/suite policy
# can reject a wrong channel, not a different signing key.
cp "$T/A.asc" /usr/share/keyrings/auros-archive-keyring.asc
mkdir -p "$T/apt-empty"
: > "$T/apt-status"
for client in desktop revive multilingual; do
    for publisher in desktop revive multilingual; do
        rm -rf "$SRV"; cp -a "$T/profile-$publisher" "$SRV"
        lists="$T/lists-$client-$publisher"
        mkdir -p "$lists/partial"
        bash "$AC" source "$URL" > "$T/profile.sources"
        opts=(-o Dir::Etc::sourcelist="$T/profile.sources" -o Dir::Etc::sourceparts="$T/apt-empty"
              -o Dir::Etc::preferences="$T/prefs-$client" -o Dir::Etc::preferencesparts="$T/apt-empty"
              -o Dir::State::status="$T/apt-status" -o Dir::State::lists="$lists" -o APT::Sandbox::User=root)
        apt-get "${opts[@]}" update > "$T/log/apt-$client-$publisher" 2>&1 || need "profile apt update failed"
        candidate=$(apt-cache "${opts[@]}" policy auros-desktop | sed -n 's/^ *Candidate: //p')
        expected='(none)'; [ "$client" = "$publisher" ] && expected=$V2
        [ "$candidate" = "$expected" ] && ok "$client apt candidate from $publisher: $expected" || bad "$client apt candidate from $publisher" "$candidate, expected $expected"
    done
done

for client in revive multilingual; do
    rm -rf "$SRV"; cp -a "$T/profile-$client" "$SRV"
    sed -i 's/^Suite: .*/Suite: stable/' "$SRV/Release"
    bash "$AC" --profile "$client" sign "$SRV" "$A" >/dev/null || need "wrong-suite signing failed"
    lists="$T/lists-$client-wrong-suite"; mkdir -p "$lists/partial"
    opts=(-o Dir::Etc::sourcelist="$T/profile.sources" -o Dir::Etc::sourceparts="$T/apt-empty"
          -o Dir::Etc::preferences="$T/prefs-$client" -o Dir::Etc::preferencesparts="$T/apt-empty"
          -o Dir::State::status="$T/apt-status" -o Dir::State::lists="$lists" -o APT::Sandbox::User=root)
    apt-get "${opts[@]}" update > "$T/log/apt-$client-wrong-suite" 2>&1 || need "wrong-suite apt update failed"
    candidate=$(apt-cache "${opts[@]}" policy auros-desktop | sed -n 's/^ *Candidate: //p')
    [ "$candidate" = '(none)' ] && ok "$client apt refuses same label with wrong suite" || bad "$client apt refuses same label with wrong suite"
done

# Only AurOS's source: Ubuntu's are put aside, so this is offline, fast,
# and nothing but our repository can be the reason for what happens.
mkdir -p /etc/apt/sources.list.d
mv /etc/apt/sources.list.d/* "$T/aside-d/" 2>/dev/null
[ -e /etc/apt/sources.list ] && mv /etc/apt/sources.list "$T/aside-list/"
mkdir -p /etc/apt/preferences.d /etc/apt/apt.conf.d /etc/apt/trusted.gpg.d /usr/share/keyrings
rm -f /etc/apt/preferences.d/auros /etc/apt/apt.conf.d/53auros-updates
[ "$BREAK" = pin ]    || cp "$ROOT/rootfs/etc/apt/preferences.d/auros" /etc/apt/preferences.d/auros
[ "$BREAK" = origin ] || cp "$ROOT/rootfs/etc/apt/apt.conf.d/53auros-updates" /etc/apt/apt.conf.d/
[ "$BREAK" = keep ] && sed -i '/keep-previous/d' /etc/apt/apt.conf.d/53auros-updates
mkdir -p /usr/lib/auros
install -m 0755 "$ROOT/rootfs/usr/lib/auros/keep-previous" "$ROOT/rootfs/usr/lib/auros/rollback" /usr/lib/auros/
cp "$T/B.asc" /etc/apt/trusted.gpg.d/another-vendor-test.asc
KR=/usr/share/keyrings/auros-archive-keyring.asc
bash "$AC" source "$URL" > /etc/apt/sources.list.d/auros.sources
case "$BREAK" in
    trusted)  echo 'Trusted: yes' >> /etc/apt/sources.list.d/auros.sources ;;
    signedby) sed -i '/^Signed-By:/d' /etc/apt/sources.list.d/auros.sources ;;
esac
grep -q "^Signed-By: $KR$" /etc/apt/sources.list.d/auros.sources || [ "$BREAK" = signedby ] ||
    need "aptchannel source does not pin $KR"

ver() { dpkg-query -W -f='${Version}' "$1" 2>/dev/null; }
# One computer per case: AurOS v1 freshly installed, the repository
# served, apt's lists fetched again, then unattended-upgrades -- the
# daily job, not a person running apt.
computer() { # CASE KEYRING [EDIT]
    local n=$1 L="$T/log/$1"
    dpkg --purge auros auros-desktop auros-config >/dev/null 2>&1
    dpkg -i "$T"/debs-v1/*.deb > "$L" 2>&1 || { cat "$L"; need "could not install AurOS v1"; }
    # As the image has it: the installed set kept at /var/lib/auros/debs.
    rm -rf /var/lib/auros/debs /var/lib/auros/debs-previous /etc/apt/preferences.d/00auros-broken-*
    mkdir -p /var/lib/auros/debs; cp "$T"/debs-v1/*.deb /var/lib/auros/debs/
    (cd /var/lib/auros/debs && sha256sum ./*.deb > SHA256SUMS)
    [ "${3:-}" = edit ] && printf 'setting=hers\n' > /etc/auros/updatetest.conf
    cp "$2" "$KR"
    daily "$n" "$L"
}
daily() { # REPO LOG: the repository served, then one day of apt-daily and apt-daily-upgrade
    local n=$1 L=$2
    rm -rf "$SRV"; cp -a "$T/r-$n" "$SRV"
    rm -rf /var/lib/apt/lists/*
    { echo "== apt-get update"; apt-get update 2>&1
      echo "== apt-cache policy"; apt-cache policy auros-desktop hello 2>&1
      echo "== unattended-upgrade"; unattended-upgrade -v 2>&1
      # u-u says only "failed to download"; apt says why.
      echo "== apt-get download"; mkdir -p "$T/dl"; (cd "$T/dl" && rm -f ./*.deb && apt-get download auros-desktop 2>&1)
      echo "== /var/log/unattended-upgrades"; cat /var/log/unattended-upgrades/unattended-upgrades.log 2>/dev/null
      : > /var/log/unattended-upgrades/unattended-upgrades.log 2>/dev/null; } >> "$L" 2>&1
}
expect() { # NAME CASE WANT_VERSION [REGEX-THE-LOG-MUST-SHOW]
    local got; got=$(ver auros-desktop)
    if [ "$got" != "$3" ]; then
        bad "$1" "auros-desktop is $got, wanted $3" "$(tail -25 "$T/log/$2" | sed 's/^/  | /')"
    elif [ -n "${4:-}" ] && ! grep -qE "$4" "$T/log/$2"; then
        bad "$1" "not for the reason expected (no /$4/ in the log)" "$(tail -25 "$T/log/$2" | sed 's/^/  | /')"
    else ok "$1"; fi
}

echo
echo "== live: apt and unattended-upgrades on a computer with AurOS $V1${BREAK:+   [--break=$BREAK]}"
computer good "$T/A.asc"
expect "a signed update installs by itself" good "$V2"
[ "$(/usr/bin/aurshell)" = two ] && [ "$(ver auros)" = "$V2" ] && [ "$(ver auros-config)" = "$V2" ] &&
    ok "  ... every part, together" || bad "  ... every part, together" "aurshell says $(/usr/bin/aurshell); auros $(ver auros); auros-config $(ver auros-config)"
[ ! -e /run/reboot-required ] && [ ! -e /var/run/reboot-required ] &&
    ok "  ... and asks for no restart" || bad "  ... and asks for no restart"

computer unsigned "$T/A.asc"
expect "unsigned: refused" unsigned "$V1" 'is not signed'
computer vendor "$T/A.asc"
expect "signed by another key this computer trusts: refused" vendor "$V1" 'NO_PUBKEY|not signed'
computer baddeb "$T/A.asc"
expect "a .deb changed after signing: refused" baddeb "$V1" 'Hash Sum mismatch|unexpected size|[Hh]ashes'
[ "$(/usr/bin/aurshell)" = one ] && ok "  ... and nothing of it ran" || bad "  ... and nothing of it ran"
computer badidx "$T/A.asc"
expect "Packages changed after signing: refused" badidx "$V1" 'Hash Sum mismatch|unexpected size|[Hh]ashes'
computer old "$T/A.asc"
expect "an older version: refused" old "$V1" "$V0"
computer foreign "$T/A.asc"
if [ "$(ver hello)" = "$HELLO" ]; then
    # And for the right reason: apt saw hello 99 in our repository, at -1.
    awk '/^hello:/{p=1} /^==/{p=0} p' "$T/log/foreign" | grep -q -- "-1 http://127.0.0.1:$PORT " &&
        ok "not AurOS's package, sealed with AurOS's key: refused" ||
        bad "not AurOS's package, sealed with AurOS's key: refused" "hello stayed, but apt did not see 99 at -1" \
            "$(awk '/^hello:/{p=1} /^==/{p=0} p' "$T/log/foreign" | sed 's/^/  | /')"
else
    bad "not AurOS's package, sealed with AurOS's key: refused" "hello is $(ver hello), was $HELLO"
fi
expect "  ... while AurOS's own update in the same repository installs" foreign "$V2"

computer newkey "$T/AC.asc"
expect "rotation: a computer with both keys takes the new key's" newkey "$V2"
computer both "$T/A.asc"
expect "rotation: signed by both, a computer with the old key takes it" both "$V2"

computer good "$T/A.asc" edit
expect "a setting edited on the computer does not stop the update" good "$V2"
[ "$(cat /etc/auros/updatetest.conf)" = 'setting=hers' ] &&
    ok "  ... and her setting is kept" || bad "  ... and her setting is kept" "$(cat /etc/auros/updatetest.conf)"

kept() { ls "$1"/auros_*_amd64.deb 2>/dev/null | sed 's#.*/auros_\(.*\)_amd64\.deb$#\1#'; }
echo
echo "== keep-previous needs nothing from apt's cache for the set it keeps"
# A file: repository, or a cache cleaned a moment after the install: the
# new .debs are not in /var/cache/apt/archives. The set that was
# installed until now is on this disk and is what rollback needs.
dpkg --purge auros auros-desktop auros-config >/dev/null 2>&1
dpkg -i "$T"/debs-v1/*.deb >/dev/null 2>&1
rm -rf /var/lib/auros/debs /var/lib/auros/debs-previous; mkdir -p /var/lib/auros/debs
cp "$T"/debs-v1/*.deb /var/lib/auros/debs/; (cd /var/lib/auros/debs && sha256sum ./*.deb > SHA256SUMS)
dpkg -i "$T"/debs-v2/*.deb >/dev/null 2>&1
AUROS_APT_CACHE=/nonexistent sh /usr/lib/auros/keep-previous
( cd /var/lib/auros/debs-previous && sha256sum -c --quiet SHA256SUMS ) >/dev/null 2>&1 && [ "$(kept /var/lib/auros/debs-previous)" = "$V1" ] &&
    ok "the set before the update is kept, intact, with none of the new files in the cache" ||
    bad "the set before the update is kept, intact, with none of the new files in the cache" "previous: $(kept /var/lib/auros/debs-previous)"
[ -z "$(kept /var/lib/auros/debs)" ] && ok "  ... and the new set is not claimed to be there" || bad "  ... and the new set is not claimed to be there" "debs: $(kept /var/lib/auros/debs)"
mkdir -p "$T/cache3"; cp "$T"/debs-v3/*.deb "$T/cache3/"
dpkg -i "$T"/debs-v3/*.deb >/dev/null 2>&1
AUROS_APT_CACHE="$T/cache3" sh /usr/lib/auros/keep-previous
[ "$(kept /var/lib/auros/debs)" = "$V3" ] && ok "  ... and the next update, with its files in the cache, is kept as the new set" ||
    bad "  ... and the next update, with its files in the cache, is kept as the new set" "debs: $(kept /var/lib/auros/debs)"

echo
echo "== a broken AurOS update is undone"
computer good "$T/A.asc"
[ "$(kept /var/lib/auros/debs)" = "$V2" ] && [ "$(kept /var/lib/auros/debs-previous)" = "$V1" ] &&
    ok "the update keeps the version before it" ||
    bad "the update keeps the version before it" "debs: $(kept /var/lib/auros/debs); previous: $(kept /var/lib/auros/debs-previous)"
# The desktop at its last safe mode, failing three more times: the
# real shell-guard, as aurshell.service's ExecStartPre runs it.
gd="$T/guard"; rm -rf "$gd"; mkdir -p "$gd"; echo 2 > "$gd/level"
for t in 10 20 30; do SERVICE_RESULT=signal AUROS_GUARD_DIR="$gd" AUROS_GUARD_NOW=$t sh "$ROOT/rootfs/usr/lib/auros/shell-guard" stopped; done
AUROS_GUARD_DIR="$gd" AUROS_GUARD_NOW=40 sh "$ROOT/rootfs/usr/lib/auros/shell-guard" start 2> "$T/log/guard"
[ "$(ver auros-desktop)" = "$V1" ] && [ "$(/usr/bin/aurshell)" = one ] && [ "$(cat "$gd/level")" = 0 ] &&
    ok "failing in its last safe mode: back to the version before" ||
    bad "failing in its last safe mode: back to the version before" "auros-desktop $(ver auros-desktop); level $(cat "$gd/level")" \
        "$(cat "$T/log/guard" /var/lib/auros/rollback.log 2>/dev/null | tail -5)"
daily good "$T/log/after-rollback"
[ "$(ver auros-desktop)" = "$V1" ] && grep -q "$V2" "$T/log/after-rollback" &&
    ok "  ... and the daily update does not bring the broken one back" ||
    bad "  ... and the daily update does not bring the broken one back" "auros-desktop $(ver auros-desktop)"
daily fixed "$T/log/fixed"
[ "$(ver auros-desktop)" = "$V3" ] && [ "$(/usr/bin/aurshell)" = three ] &&
    ok "  ... while a newer fix installs by itself" ||
    bad "  ... while a newer fix installs by itself" "auros-desktop $(ver auros-desktop)" "$(tail -15 "$T/log/fixed" | sed 's/^/  | /')"

echo
if [ "$fail" -eq 0 ]; then echo "updatetest: $checked checks, all passed${BREAK:+ (with --break=$BREAK: the test did NOT catch it)}"; exit 0; fi
echo "updatetest: $fail of $checked checks FAILED${BREAK:+ (--break=$BREAK, as it should)}"
exit 1
