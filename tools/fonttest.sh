#!/bin/sh
# fonttest.sh — does every word on the desktop come out in the weight
# the theme asked for?
#
# It did not. build/forge's font resolver, asked for a face by a plain
# family name ("Inter"), matched `Inter*` and took the first file in
# sort order: Inter-Black.otf. Every theme inherits nocturne's faces,
# so the whole desktop -- headings, names, every hint -- was drawn in
# the heaviest cut of the family, on every image. Nothing failed: the
# file is a good font, it loaded, it drew.
#
# This runs forge's own resolver (cut out of build/forge, not copied)
# against a pretend font directory holding the cuts Ubuntu's
# fonts-inter ships, and checks where each name the themes use points.
#
#   sh tools/fonttest.sh            (from the repository root; no root)
set -u
cd "$(dirname "$0")/.." || exit 2
T=$(mktemp -d "${TMPDIR:-/tmp}/fonttest.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT
fail=0
ok() { if [ "$2" = 0 ]; then echo "    ok    $1"; else echo "    FAIL  $1"; fail=1; fi; }

F="$T/usr/share/fonts/opentype/inter"
mkdir -p "$F" "$T/usr/share/fonts/truetype/dejavu"
for w in Black BlackItalic Bold ExtraBold Light Medium Regular SemiBold Thin; do
    printf 'OTTO\0\0CFF \0\0head' > "$F/Inter-$w.otf"
    printf 'OTTO\0\0CFF \0\0head' > "$F/InterDisplay-$w.otf"
done
printf '\0\1\0\0glyf\0fvar' > "$T/usr/share/fonts/truetype/dejavu/InterVariable.ttf"
printf '\0\1\0\0glyf\0head' > "$T/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
printf '\0\1\0\0glyf\0head' > "$T/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

a=$(grep -n '^    resolve_font() {' build/forge | cut -d: -f1)
b=$(grep -n '# The six aurinit service files' build/forge | cut -d: -f1)
[ -n "$a" ] && [ -n "$b" ] || { echo "cannot find the resolver in build/forge"; exit 2; }
{
    echo 'note() { :; }'
    echo "RFS='$T' ROOT='$PWD' theme=nocturne"
    echo 'install -d "$RFS/usr/share/auros/fonts"'
    sed -n "${a},$((b-1))p" build/forge
} > "$T/resolve.sh"
sh "$T/resolve.sh" || { echo "the resolver failed"; exit 1; }

target() { t=$(readlink "$T/usr/share/auros/fonts/$1.ttf" 2>/dev/null); basename "${t:-none}"; }

echo "fonttest: the faces every theme names"
for key in font_display font_text font_text_bold font_sans; do
    name=$(sed -nE "s/^$key=\"([^\"]*)\".*/\\1/p" themes/nocturne.theme | head -1)
    got=$(target "$name")
    case "$name" in
        *-*) want="$name.otf" ;;           # a named cut: exactly that file
        *)   want="$name-Regular.otf" ;;   # a family name: its Regular
    esac
    [ "$got" = "$want" ]; ok "$key=$name -> $got (want $want)" $?
done
# The reported failure, said plainly.
for l in "$T"/usr/share/auros/fonts/*.ttf; do
    case "$(readlink "$l")" in *Black*|*Thin*|*Light*|*Italic*)
        ok "$(basename "$l") is not a display-only or italic cut" 1 ;; esac
done
# There is weight contrast: headings and names are not the body face.
d=$(target "$(sed -nE 's/^font_display="([^"]*)".*/\1/p' themes/nocturne.theme)")
t=$(target "$(sed -nE 's/^font_text="([^"]*)".*/\1/p' themes/nocturne.theme)")
[ "$d" != "$t" ]; ok "headings ($d) are a different weight from text ($t)" $?

[ $fail = 0 ] && echo "fonttest: all good" || echo "fonttest: FAILED"
exit $fail
