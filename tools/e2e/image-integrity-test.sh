#!/bin/bash
# Run e2e.yml's real image-download step against local release fixtures.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$ROOT/.github/workflows/e2e.yml" "$TMP/step.sh" <<'PY'
import sys

text = open(sys.argv[1], encoding="utf-8").read()
marker = "      - name: the image under test (a build run's pieces, else the release's)\n"
try:
    block = text.split(marker, 1)[1].split("        run: |\n", 1)[1]
    block = block.split("\n      - name:", 1)[0]
except IndexError:
    raise SystemExit("image step not found")
lines = block.splitlines()
if not lines or any(line and not line.startswith("          ") for line in lines):
    raise SystemExit("image step has unexpected indentation")
open(sys.argv[2], "w", encoding="utf-8").write(
    "\n".join(line[10:] for line in lines) + "\n"
)
PY

mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" <<'SH'
#!/bin/sh
set -eu
dest=
pieces=0
image_sha=0
image_parts=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        -D) dest=$2; shift 2 ;;
        -p)
            case "$2" in
                pieces.txt) pieces=1 ;;
                image.sha256) image_sha=1 ;;
                'auros-desktop.img.gz.*') image_parts=1 ;;
                *) echo "fake gh: unexpected pattern $2" >&2; exit 2 ;;
            esac
            shift 2
            ;;
        *) shift ;;
    esac
done
[ -n "$dest" ]
[ "$image_parts" -eq 0 ] || cp "$FIXTURE"/auros-desktop.img.gz.* "$dest"/
[ "$pieces" -eq 0 ] || [ ! -e "$FIXTURE/pieces.txt" ] || cp "$FIXTURE/pieces.txt" "$dest"/
[ "$image_sha" -eq 0 ] || [ ! -e "$FIXTURE/image.sha256" ] || cp "$FIXTURE/image.sha256" "$dest"/
SH
chmod +x "$TMP/bin/gh"

# Ubuntu has GNU sha256sum. Only provide a stream-compatible adapter on hosts
# such as macOS whose checksum tool lacks the GNU checking interface.
empty_sha=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
: > "$TMP/empty"
if ! printf '%s  %s\n' "$empty_sha" "$TMP/empty" | sha256sum -c --quiet >/dev/null 2>&1; then
    cat > "$TMP/bin/sha256sum" <<'PY'
#!/usr/bin/env python3
import hashlib
import re
import sys

args = sys.argv[1:]
if not args or args.pop(0) != "-c":
    raise SystemExit("checksum adapter supports only sha256sum -c")
quiet = False
if args and args[0] == "--quiet":
    quiet = True
    args.pop(0)
if len(args) > 1:
    raise SystemExit("checksum adapter received unexpected arguments")
source = open(args[0], encoding="utf-8") if args else sys.stdin
lines = source.read().splitlines()
if source is not sys.stdin:
    source.close()
if not lines:
    raise SystemExit("sha256sum: no properly formatted checksum lines found")
failed = False
for line in lines:
    match = re.fullmatch(r"([0-9A-Fa-f]{64}) [ *](.+)", line)
    if not match:
        failed = True
        continue
    expected, path = match.groups()
    try:
        with open(path, "rb") as checked:
            digest = hashlib.sha256()
            for chunk in iter(lambda: checked.read(1024 * 1024), b""):
                digest.update(chunk)
            actual = digest.hexdigest()
    except OSError as error:
        print(f"sha256sum: {path}: {error}", file=sys.stderr)
        failed = True
        continue
    if actual.lower() != expected.lower():
        print(f"{path}: FAILED", file=sys.stderr)
        failed = True
    elif not quiet:
        print(f"{path}: OK")
raise SystemExit(1 if failed else 0)
PY
    chmod +x "$TMP/bin/sha256sum"
fi

make_fixture() {
    dest=$1
    mkdir -p "$dest"
    printf 'AurOS image fixture\n' > "$dest/raw.img"
    gzip -c "$dest/raw.img" > "$dest/auros-desktop.img.gz.000"
    piece_sha=$(sha256sum "$dest/auros-desktop.img.gz.000" | awk '{print $1}')
    raw_sha=$(sha256sum "$dest/raw.img" | awk '{print $1}')
    printf 'piece-list\npiece auros-desktop.img.gz.000 %s %s\n' \
        "$(wc -c < "$dest/auros-desktop.img.gz.000" | tr -d ' ')" "$piece_sha" > "$dest/pieces.txt"
    printf '%s\n' "$raw_sha" > "$dest/image.sha256"
    rm "$dest/raw.img"
}

run_step() {
    fixture=$1
    run=$2
    mkdir -p "$run"
    PATH="$TMP/bin:$PATH" FIXTURE="$fixture" E2E_DIR="$run" \
        GITHUB_REPOSITORY=example/auros IN_URL=https://github.com/example/auros/releases/download/test/AurOS-Setup.exe \
        BUILD_RUN= bash -e -o pipefail "$TMP/step.sh" > "$run.log" 2>&1 || {
            status=$?
            [ "${SHOW_FAILURE:-0}" = 0 ] || cat "$run.log" >&2
            return "$status"
        }
}

valid="$TMP/valid"
make_fixture "$valid"
SHOW_FAILURE=1 run_step "$valid" "$TMP/run-valid"
cmp <(printf 'AurOS image fixture\n') "$TMP/run-valid/auros.img"
[ ! -e "$TMP/run-valid/pieces" ]
echo "image-integrity: valid metadata and image pass"

corrupt="$TMP/corrupt"
cp -R "$valid" "$corrupt"
printf 'corrupt' >> "$corrupt/auros-desktop.img.gz.000"
if run_step "$corrupt" "$TMP/run-corrupt"; then
    echo "image-integrity: corrupted piece unexpectedly passed" >&2
    exit 1
fi
[ ! -e "$TMP/run-corrupt/auros.img" ]
echo "image-integrity: corrupted piece fails before reconstruction"

wrong="$TMP/wrong-raw"
cp -R "$valid" "$wrong"
printf '%064d\n' 0 > "$wrong/image.sha256"
if run_step "$wrong" "$TMP/run-wrong"; then
    echo "image-integrity: wrong raw digest unexpectedly passed" >&2
    exit 1
fi
[ -e "$TMP/run-wrong/auros.img" ]
[ -e "$TMP/run-wrong/pieces" ]
echo "image-integrity: wrong raw digest fails before cleanup"

missing="$TMP/missing"
cp -R "$valid" "$missing"
rm "$missing/pieces.txt" "$missing/image.sha256"
if run_step "$missing" "$TMP/run-missing"; then
    echo "image-integrity: missing metadata unexpectedly passed" >&2
    exit 1
fi
[ ! -e "$TMP/run-missing/auros.img" ]
echo "image-integrity: missing metadata fails"
