#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  The Windows side's checks, in shell: the reference for anything that
#  consumes a handover volume (the put-back task's PowerShell does the
#  same things against the same bytes).
#
#    sh tests/verify-handover.sh MOUNTED-VOLUME-OR-FOLDER
#
#  A volume is OK to consume only if all of this holds:
#   - AurOS-handover/version is 1, and DONE exists and starts with "ok"
#     (DONE is the only word for complete);
#   - manifest.sha256 is the SHA-256 of manifest.tsv, and manifest.tsv
#     starts with the line "#ferry-handover<TAB>1" (other "#" lines are
#     information: #windows-user, #aur-user, #created);
#   - every manifest row (FOLDER PATH SIZE SHA256 MTIME, tab-separated)
#     names a file under files/ with that size and that SHA-256, and
#     nothing is under files/ that the manifest does not list;
#   - every PATH is a legal Windows name (no : * ? " < > | \ or control
#     characters, no trailing dot or space, no CON/PRN/AUX/NUL/COM1-9/
#     LPT1-9) and no two paths in a folder differ only in capitals --
#     because the consumer will not rename anything.
#  Prints one "PROBLEM:" line per problem, then "ok N files" (exit 0) or
#  "not ok" (exit 1). Writes nothing on the volume.
# ═══════════════════════════════════════════════════════════════════
set -u
V=${1:?usage: verify-handover.sh VOLUME}
H="$V/AurOS-handover"
W=$(mktemp -d "${TMPDIR:-/tmp}/verifyho.XXXXXX")
trap 'rm -rf "$W"' EXIT INT TERM HUP
: > "$W/problems"
problem() { printf 'PROBLEM: %s\n' "$*" >> "$W/problems"; }

[ "$(cat "$H/version" 2>/dev/null)" = 1 ] || problem "version is not 1"
head -1 "$H/DONE" 2>/dev/null | grep -qx ok || problem "no DONE: the volume is not complete"
if [ ! -f "$H/manifest.tsv" ]; then
    problem "no manifest.tsv"
else
    want=$(cut -d' ' -f1 "$H/manifest.sha256" 2>/dev/null)
    have=$(sha256sum "$H/manifest.tsv" | cut -d' ' -f1)
    [ -n "$want" ] && [ "$want" = "$have" ] || problem "manifest.sha256 does not match manifest.tsv"
    head -1 "$H/manifest.tsv" | grep -q '^#ferry-handover	1$' || problem "manifest.tsv does not start with #ferry-handover 1"

    # sizes and hashes of everything listed
    awk -F'\t' '!/^#/ { printf "%s  %s/%s\n", $4, $1, $2 }' "$H/manifest.tsv" > "$W/list"
    if [ -s "$W/list" ]; then
        ( cd "$H/files" 2>/dev/null && sha256sum -c --quiet "$W/list" 2>&1 ) > "$W/hashes" || true
        grep -v '^$' "$W/hashes" | head -20 | while IFS= read -r l; do problem "$l"; done
        # a file that is missing or wrong is reported by sha256sum -c; the size is checked too
        awk -F'\t' '!/^#/ { print $3 "\t" $1 "/" $2 }' "$H/manifest.tsv" | while IFS='	' read -r size path; do
            got=$(wc -c < "$H/files/$path" 2>/dev/null | tr -d ' ')
            [ "$got" = "$size" ] || echo "size of $path is ${got:-missing}, the manifest says $size"
        done | head -20 | while IFS= read -r l; do problem "$l"; done
    fi

    # nothing that the manifest does not list
    ( cd "$H/files" 2>/dev/null && find . -type f | sed 's,^\./,,' ) | LC_ALL=C sort > "$W/found"
    awk -F'\t' '!/^#/ { print $1 "/" $2 }' "$H/manifest.tsv" | LC_ALL=C sort > "$W/listed"
    comm -23 "$W/found" "$W/listed" | head -5 | while IFS= read -r l; do problem "a file the manifest does not list: $l"; done

    # legal Windows names, nothing to rename
    awk -F'\t' '
        /^#/ { next }
        { m = split($2, part, "/")
          for (i = 1; i <= m; i++) {
              p = part[i]
              if (p ~ /[<>:"\\|?*]/ || p ~ /[\001-\037]/) print "illegal character in " $2
              if (p ~ /[. ]$/) print "trailing dot or space in " $2
              stem = toupper(p); sub(/\..*/, "", stem)
              if (stem ~ /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/) print "reserved name in " $2
          }
          key = tolower($1 "/" $2)
          if (key in seen) print "names differ only in capitals: " $2 " and " seen[key]
          seen[key] = $2 }' "$H/manifest.tsv" | head -20 | while IFS= read -r l; do problem "$l"; done
fi

n=$( [ -f "$H/manifest.tsv" ] && grep -vc '^#' "$H/manifest.tsv" || echo 0)
if [ ! -s "$W/problems" ]; then echo "ok $n files"; exit 0; fi
cat "$W/problems"; echo "not ok"; exit 1
