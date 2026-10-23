#!/bin/sh
# sh tools/signed-check.sh UNSIGNED SIGNED
#
# What comes back from a signing service is not trusted because it came
# back. release.yml's `sign` job runs this on the file SignPath returns,
# and publishes nothing unless every line below passes:
#
#   1. UNSIGNED really is unsigned, and SIGNED really is signed;
#   2. the signature verifies, against the system's trusted roots
#      (SignPath's certificate chains to a public authority; the
#      timestamp is checked too);
#   3. the signer is who it should be: the first signer's subject names
#      $AUROS_SIGN_SUBJECT (default "SignPath Foundation");
#   4. the Authenticode digest of SIGNED equals UNSIGNED's
#      (tools/authenticode.py): the signed file is the file that was
#      built plus a signature, byte for byte nothing else. A valid
#      signature alone would also be on a file that is not ours.
#
# Environment (tests only): AUROS_SIGN_CAFILE the trusted roots (default
# the system bundle), AUROS_SIGN_NO_TS=1 accept a signature without a
# timestamp, AUROS_SIGN_SUBJECT, OSSLSIGNCODE.
set -eu
cd "$(dirname "$0")/.."

[ $# -eq 2 ] || { echo "usage: sh tools/signed-check.sh UNSIGNED SIGNED" >&2; exit 2; }
U=$1; S=$2
refuse() { echo "signed-check: REFUSED: $*" >&2; exit 1; }
ok()     { printf '  ok  %s\n' "$*"; }

[ -f "$U" ] && [ -f "$S" ] || refuse "a file is missing"
AC="python3 -I tools/authenticode.py"

# 1
if $AC signed "$U" 2>/dev/null; then refuse "the file given as unsigned already carries a signature"; fi
$AC signed "$S" 2>/dev/null || refuse "the file given as signed carries no signature"
ok "the build's file is unsigned, the returned file is signed"

# 2
: "${AUROS_SIGN_CAFILE:=/etc/ssl/certs/ca-certificates.crt}"
export AUROS_SIGN_CAFILE
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/signedcheck.XXXXXX")
trap 'rm -rf "$TMPD"' EXIT
tool=${OSSLSIGNCODE:-osslsigncode}
ts=""; [ "${AUROS_SIGN_NO_TS:-0}" = 1 ] && ts="-ignore-timestamp"
# shellcheck disable=SC2086
"$tool" verify -CAfile "$AUROS_SIGN_CAFILE" $ts -in "$S" > "$TMPD/verify.txt" 2>&1 \
    || { sed -n '1,25p' "$TMPD/verify.txt" >&2; refuse "the signature does not verify against the trusted roots"; }
ok "the signature verifies against the trusted roots"

# 3: the subject of the first signer, not the issuer's, not a later one.
want=${AUROS_SIGN_SUBJECT:-SignPath Foundation}
got=$(awk '/Signer #0:/ {f=1} f && /Subject:/ {print; exit}' "$TMPD/verify.txt")
case "$got" in
    *"$want"*) ok "signed by: ${got#*Subject: }" ;;
    *) refuse "the signer is \"${got#*Subject: }\", not \"$want\"" ;;
esac

# 4
du=$($AC digest "$U"); ds=$($AC digest "$S")
[ "$du" = "$ds" ] || refuse "the signed file is not the built file: Authenticode digest $ds, built $du"
ok "the signed file is the built file plus a signature (Authenticode SHA-256 $du)"

echo "signed-check: ok. unsigned sha256 $(sha256sum "$U" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$U" | cut -d' ' -f1), signed sha256 $(sha256sum "$S" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$S" | cut -d' ' -f1)"
