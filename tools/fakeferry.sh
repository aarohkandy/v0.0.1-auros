#!/bin/sh
# Stand-in for Ferry: `handover --need`, and `back` writing docs/FERRY.md's layout
# into the volume named by FERRY_HANDOVER_DEV.
case "$1" in
handover) echo "${FAKE_NEED:-$(( $(du -sb "$FAKE_HOME" | cut -f1) + 1048576 ))}" ;;
back)
    m=$(mktemp -d); mount -t exfat "$FERRY_HANDOVER_DEV" "$m" || exit 5
    r="$m/AurOS-handover"; mkdir -p "$r/files"
    printf '1\n' > "$r/version"
    { printf '#ferry-handover\t1\n#windows-user\tBob\n'
      (cd "$FAKE_HOME" && find . -type f | sed 's#^\./##' | sort | while IFS= read -r f; do
          d=${f%%/*}; p=${f#*/}
          mkdir -p "$r/files/$d/$(dirname "$p")"; cp "$FAKE_HOME/$f" "$r/files/$d/$p"
          printf '%s\t%s\t%s\t%s\t1700000000\n' "$d" "$p" "$(stat -c %s "$FAKE_HOME/$f")" "$(sha256sum "$FAKE_HOME/$f" | cut -d' ' -f1)"
      done)
    } > "$r/manifest.tsv"
    ( cd "$r" && sha256sum manifest.tsv ) > "$r/manifest.sha256"
    printf 'ok\nfiles=3\nbytes=0\nat=now\n' > "$r/DONE"
    sync; umount "$m"; rmdir "$m" ;;
esac
