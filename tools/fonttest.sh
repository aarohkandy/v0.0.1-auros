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
trap 'rm -rf "$T"' EXIT INT TERM
fail=0
ok() { if [ "$2" = 0 ]; then echo "    ok    $1"; else echo "    FAIL  $1"; fail=1; fi; }

F="$T/usr/share/fonts/opentype/inter"
mkdir -p "$F" "$T/usr/share/fonts/truetype/dejavu"
for w in Black BlackItalic Bold ExtraBold Light Medium Regular SemiBold Thin; do
    printf 'OTTO\0\0CFF \0\0head' > "$F/Inter-$w.otf"
    printf 'OTTO\0\0CFF \0\0head' > "$F/InterDisplay-$w.otf"
done
for w in Regular Bold Italic BoldItalic; do          # fonts-atkinson-hyperlegible's four files
    mkdir -p "$T/usr/share/fonts/opentype/atkinson-hyperlegible"
    printf 'OTTO\0\0CFF \0\0head' > "$T/usr/share/fonts/opentype/atkinson-hyperlegible/AtkinsonHyperlegible-$w.otf"
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

# And the shell READS a theme's font by name. open_font() used to load
# only absolute paths, so a theme that said font_text="X" (a .theme
# file, or shellpreview, which reads one) got the first fallback face.
echo "fonttest: the shell opens a face by the name a theme gives it"
M=/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
if [ -f "$M" ] && command -v cc >/dev/null 2>&1; then
    mkdir -p "$T/fonts"; ln -s "$M" "$T/fonts/Narrow.ttf"
    cat > "$T/name.c" <<'EOC'
#include <stdio.h>
#include <string.h>
#include "shell.h"
int main(int argc, char **argv)
{
    theme_t t = {0};
    if (theme_load(&t, argv[1]) < 0) return 2;
    shell_ctx c; memset(&c, 0, sizeof c);
    shell_theme_load(&c, &t);
    c.text_scale = 1.f;
    shell_fonts f; shell_fonts_load(&f, &c);
    font *want = font_load(argv[2], 14.f);
    if (!f.small || !want) return 2;
    float a = font_text_width(f.small, "iiiiWWWW"), b = font_text_width(want, "iiiiWWWW");
    printf("%.1f %.1f\n", a, b);
    return (a > b - 0.5f && a < b + 0.5f) ? 0 : 1;
}
EOC
    printf 'font_text = Narrow\nfont_size = 14\n' > "$T/theme.conf"
    if cc -O2 -std=gnu11 -o "$T/name" "$T/name.c" -DAUROS_FONT_DIR="\"$T/fonts\"" \
           -I src/aurshell -I src/common src/aurshell/shellcommon.c src/aurshell/draw.c \
           src/aurshell/anim.c src/aurshell/layouts/*.c src/common/theme.c \
           src/common/font.c -lm 2>"$T/cc.log"; then
        "$T/name" "$T/theme.conf" "$M" >/dev/null; ok "font_text=\"Narrow\" draws in the face named Narrow" $?
    else
        cat "$T/cc.log"; ok "the name check builds" 1
    fi
else
    echo "    (skipped: no DejaVu Sans Mono or no compiler here)"
fi

[ $fail = 0 ] && echo "fonttest: all good" || echo "fonttest: FAILED"
exit $fail
