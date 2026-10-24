#!/bin/sh
# Does the theme engine actually resolve everything it is handed?
#
# aurora is the reskinning contract: a theme is a file, and every
# template in the tree must come out the other side with no token left
# in it. This existed as an assumption until a booted image showed
# `@accent|lighten:18|0x@` sitting in /etc/auros/shell.conf as a
# literal string -- the substitution regex allowed exactly ONE
# `|operation` per token, and five keys in every generated theme used
# two (derive a colour, then format it). The shell read those as
# colours, got zero, and painted with them. Nothing failed loudly.
#
# Run from the repository root. Exits non-zero on any unresolved token.
set -eu
cd "$(dirname "$0")/.."
AURORA="./src/aurora/aurora"
# A scratch state directory, so the test never touches an installed
# machine's active theme.
STATE=$(mktemp -d)
trap 'rm -rf "$STATE"' EXIT
export AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates
export AURORA_STATE="$STATE" AURORA_CACHE="$STATE/cache"

# The colour algebra is tested against a fixture with fixed values, not
# against a shipped theme. It used to use Nocturne's, so redesigning
# Nocturne's palette failed twelve assertions about arithmetic that had
# not changed -- a test that breaks when the thing it is not testing
# changes is a test people learn to ignore.
mkdir -p "$STATE/themes"
cp themes/*.theme "$STATE/themes/" 2>/dev/null || :
cat > "$STATE/themes/_algebra.theme" <<'FIXTURE'
theme_name="Algebra Fixture"
theme_variant="dark"
bg="#0B0E14"
bg_alt="#10151F"
surface="#161C28"
surface_hi="#1F2735"
overlay="#2B3542"
muted="#55606E"
subtle="#8793A4"
fg="#D4DCEA"
fg_hi="#F3F7FD"
accent="#7DD3C0"
accent_alt="#A78BFA"
accent_warm="#F2B880"
ok="#7DD3C0"
warn="#F2B880"
err="#F2788D"
info="#82AAFF"
FIXTURE
printf '_algebra\n' > "$STATE/theme"

fail=0
checked=0

# 1. The algebra itself, including chains and the error paths.
probe() {
    got=$(printf '%s\n' "$1" | $AURORA render 2>/dev/null)
    checked=$((checked + 1))
    if [ "$got" != "$2" ]; then
        printf '  FAIL  %-34s got "%s", want "%s"\n' "$1" "$got" "$2"
        fail=1
    else
        printf '  ok    %-34s %s\n' "$1" "$got"
    fi
}

echo "colour algebra (against a fixture, not a shipped theme)"
AURORA_THEMES_REAL=themes
export AURORA_THEMES="$STATE/themes"
probe '@accent|0x@'                    '0x7DD3C0'
probe '@accent|hex@'                   '7DD3C0'
probe '@accent|rgb@'                   '125,211,192'
probe '@accent|lighten:18|0x@'         '0x94DBCB'
probe '@accent|darken:20|lighten:10|0x@' '0x74B2A4'
probe '@bg_alt|mix:bg:20|0x@'          '0x0F141D'
probe '@accent|on|0x@'                 '0x0B0E14'
probe '@bg|on|0x@'                     '0xF3F7FD'
probe '@nosuchkey|0x@'                 '@@MISSING:nosuchkey@@'
probe '@accent|nosuchop@'              '@@BADOP:nosuchop@@'
probe 'plain text, no tokens'          'plain text, no tokens'
probe '@accent|0x@ and @bg|0x@'        '0x7DD3C0 and 0x0B0E14'

export AURORA_THEMES="$AURORA_THEMES_REAL"

# 2. Every template against every theme, which is the real contract.
#    `aurora render` uses the ACTIVE theme, so the active theme is
#    switched for each one and restored afterwards. Rendering every
#    template against a single theme would miss a theme that simply
#    does not define a key the templates use.
echo
echo "every template x every theme"
for th in themes/*.theme; do
    n=$(basename "$th" .theme)
    printf '%s\n' "$n" > "$STATE/theme"
    bad_total=0
    for tpl in themes/templates/*.tpl; do
        checked=$((checked + 1))
        out=$(tail -n +2 "$tpl" | $AURORA render 2>/dev/null || true)
        # An unresolved token is either our own error marker or a
        # literal @token| that the regex declined to match.
        bad=$(printf '%s' "$out" | grep -c '@@\|@[a-z_][a-z0-9_]*|' || true)
        if [ "$bad" != "0" ]; then
            printf '  FAIL  %-12s %-22s %s unresolved\n' "$n" "$(basename "$tpl")" "$bad"
            printf '%s' "$out" | grep -n '@@\|@[a-z_][a-z0-9_]*|' | head -3 | sed 's/^/          /'
            fail=1; bad_total=$((bad_total + bad))
        fi
    done
    [ "$bad_total" = 0 ] && printf '  ok    %-12s all templates resolve\n' "$n"
done
printf 'nocturne\n' > "$STATE/theme"

# 4. Text on a coloured fill is readable, in every theme. `@X|on@` is the colour for text on X;
#    it said `lum(X) > 0.55 ? bg : fg_hi`, which assumes a dark theme, so on the Light theme
#    (the default) GNOME Software's Install button was ink on dark green. Every X the templates
#    put |on beside is rendered for every theme and the pair must have 4.5:1 (WCAG AA).
echo
echo "text on a fill: 4.5:1 or better, every theme"
ratio() { # hex hex (no #) -> contrast ratio
    awk -v a="$1" -v b="$2" '
        function h(s,i){ return index("0123456789ABCDEF", substr(s,i,1)) - 1 }
        function ch(s,i){ return h(s,i)*16 + h(s,i+1) }
        function lin(v){ v = v/255; return v <= 0.03928 ? v/12.92 : exp(2.4*log((v+0.055)/1.055)) }
        function L(s){ return 0.2126*lin(ch(s,1)) + 0.7152*lin(ch(s,3)) + 0.0722*lin(ch(s,5)) }
        BEGIN { x = L(toupper(a)); y = L(toupper(b)); if (x < y) { t = x; x = y; y = t }
                printf "%.2f\n", (x+0.05)/(y+0.05) }'
}
fills=$(cat themes/templates/*.tpl | grep -o '@[a-z_][a-z0-9_]*|on[|@]' | sed 's/^@//; s/|on.*//' | sort -u)
[ -n "$fills" ] || { echo "  FAIL  no @X|on@ in any template (this test would prove nothing)"; fail=1; }
for th in themes/*.theme; do
    n=$(basename "$th" .theme)
    printf '%s\n' "$n" > "$STATE/theme"
    for x in $fills; do
        fill=$(printf '@%s|hex@' "$x" | $AURORA render 2>/dev/null)
        text=$(printf '@%s|on|hex@' "$x" | $AURORA render 2>/dev/null)
        checked=$((checked + 1))
        r=$(ratio "$text" "$fill")
        if awk -v r="$r" 'BEGIN { exit !(r >= 4.5) }'; then
            printf '  ok    %-12s text on %-6s %s on %s  %s:1\n' "$n" "$x" "$text" "$fill" "$r"
        else
            printf '  FAIL  %-12s text on %-6s %s on %s  %s:1 (4.5 needed)\n' "$n" "$x" "$text" "$fill" "$r"
            fail=1
        fi
    done
    # The shell's own C code does not use on(): the words on a primary button (welcome.c, lockpin.c)
    # are the theme's bg, on the accent. Same floor.
    fill=$(printf '@accent|hex@' | $AURORA render 2>/dev/null)
    paper=$(printf '@bg|hex@' | $AURORA render 2>/dev/null)
    checked=$((checked + 1))
    r=$(ratio "$paper" "$fill")
    if awk -v r="$r" 'BEGIN { exit !(r >= 4.5) }'; then
        printf '  ok    %-12s the shell'"'"'s primary button (bg on accent)  %s on %s  %s:1\n' "$n" "$paper" "$fill" "$r"
    else
        printf '  FAIL  %-12s the shell'"'"'s primary button (bg on accent)  %s on %s  %s:1 (4.5 needed)\n' "$n" "$paper" "$fill" "$r"
        fail=1
    fi
done
printf 'nocturne\n' > "$STATE/theme"

# 5. Every other pair gtk.css and terminal.conf draw, in every theme: text on its ground at 4.5:1
#    (disabled text and anything that only has to be found: 3:1), and the sixteen terminal colours
#    against the terminal background. (Black, white and bright white are exempt: they are ink and
#    paper by definition, and a program that prints paper on paper has chosen to.)
echo
echo "gtk.css and terminal pairs, every theme"
cat > "$STATE/pairs" <<'PAIRS'
body text on the window (fg on bg);fg;bg;4.5
entry text (fg_hi on bg);fg_hi;bg;4.5
button text (fg on surface);fg;surface;4.5
a hovered button or row (fg on surface_hi);fg;surface_hi;4.5
header bar, top (fg_hi on bg_alt);fg_hi;bg_alt;4.5
header bar, bottom (fg_hi on bg_alt darkened);fg_hi;bg_alt|darken:6;4.5
a tooltip (fg_hi on surface_hi);fg_hi;surface_hi;4.5
selected text (fg_hi on the 35% accent selection);fg_hi;accent|mix:bg:65;4.5
a disabled button (muted on surface darkened);muted;surface|darken:4;3.0
the border of an entry or a button (overlay on bg);overlay;bg;3.0
the border of a menu or a tooltip (overlay on surface);overlay;surface;3.0
a scrollbar slider (overlay lightened on bg);overlay|lighten:10;bg;3.0
the focus ring (the accent, drawn solid, on bg);accent;bg;3.0
the accent on a card (a switch, a progress bar);accent;surface;3.0
PAIRS
for th in themes/*.theme; do
    n=$(basename "$th" .theme)
    printf '%s\n' "$n" > "$STATE/theme"
    : > "$STATE/probe"
    while IFS=';' read -r what fgt bgt need; do
        printf '@%s|hex@ @%s|hex@\n' "$fgt" "$bgt" >> "$STATE/probe"
    done < "$STATE/pairs"
    $AURORA render < "$STATE/probe" > "$STATE/probe.out" 2>/dev/null
    paste -d';' "$STATE/pairs" "$STATE/probe.out" > "$STATE/pairs.out"
    bad_here=0
    while IFS=';' read -r what fgt bgt need colours; do
        set -- $colours
        checked=$((checked + 1))
        r=$(ratio "$1" "$2")
        if awk -v r="$r" -v n="$need" 'BEGIN { exit !(r >= n) }'; then :
        else printf '  FAIL  %-12s %s  %s on %s  %s:1 (%s needed)\n' "$n" "$what" "$1" "$2" "$r" "$need"; fail=1; bad_here=1; fi
    done < "$STATE/pairs.out"
    # the sixteen terminal colours against its background
    bgc=$(printf '@bg|hex@' | $AURORA render 2>/dev/null)
    for i in 1 2 3 4 5 6 8 9 10 11 12 13 14; do
        c=$(printf '@ansi%s|hex@' "$i" | $AURORA render 2>/dev/null)
        checked=$((checked + 1))
        r=$(ratio "$c" "$bgc")
        if awk -v r="$r" 'BEGIN { exit !(r >= 4.5) }'; then :
        else printf '  FAIL  %-12s terminal colour %-2s %s on %s  %s:1 (4.5 needed)\n' "$n" "$i" "$c" "$bgc" "$r"; fail=1; bad_here=1; fi
    done
    [ "$bad_here" = 0 ] && printf '  ok    %-12s every pair clears its floor\n' "$n"
done
printf 'nocturne\n' > "$STATE/theme"

# 6. A C default is a theme too. The lock screen falls back to these when the shell's conf lacks
#    a key; they were the old Nocturne teal, the one place the default look was not Light.
#    Every theme_color(&t, "KEY", 0xRRGGBB) default in the shell's C must be sandstone's own.
echo
echo "C colour defaults are the Light tokens"
for src in src/aurshell/*.c; do
    grep -o 'theme_color(&t, "[a-z_]*", *0x[0-9A-Fa-f]*)' "$src" 2>/dev/null | sed "s|^|$src |"
done | while read -r src call; do
    key=$(printf '%s' "$call" | sed 's/.*"\([a-z_]*\)".*/\1/')
    hex=$(printf '%s' "$call" | sed 's/.*0x\([0-9A-Fa-f]*\)).*/\1/' | tr a-f A-F)
    want=$(grep -oE "(^|[[:space:]])$key=\"#[0-9A-Fa-f]+\"" themes/sandstone.theme | head -1 | sed 's/.*#//; s/"//' | tr a-f A-F)
    printf '%s %s %s %s\n' "$src" "$key" "$hex" "$want"
done > "$STATE/defaults"
while read -r src key hex want; do
    checked=$((checked + 1))
    if [ -z "$want" ]; then printf '  ok    %s "%s" (no Light token of that name: not a colour of the look)\n' "$src" "$key"
    elif [ "$hex" = "$want" ]; then printf '  ok    %s "%s" is 0x%s, the Light token\n' "$src" "$key" "$hex"
    else printf '  FAIL  %s "%s" defaults to 0x%s; the Light token is #%s\n' "$src" "$key" "$hex" "$want"; fail=1; fi
done < "$STATE/defaults"

# 3. The target ships mawk, not gawk. They must agree byte for byte --
#    a gawk extension slipping in here (strtonum) cost a silent theming
#    failure on the first real image build. This runs aurora twice with
#    `awk` pointed at each, rather than trusting whichever is default.
echo
echo "mawk and gawk agree"
if command -v mawk >/dev/null 2>&1 && command -v gawk >/dev/null 2>&1; then
    shim=$(mktemp -d)
    render_with() {
        ln -sf "$(command -v "$1")" "$shim/awk"
        PATH="$shim:$PATH" $AURORA render
    }
    a=$(cat themes/templates/*.tpl | render_with mawk 2>/dev/null | md5sum)
    b=$(cat themes/templates/*.tpl | render_with gawk 2>/dev/null | md5sum)
    checked=$((checked + 1))
    rm -rf "$shim"
    if [ "$a" = "$b" ]; then
        echo "  ok    identical output  ($a)"
    else
        echo "  FAIL  mawk $a"
        echo "        gawk $b"
        fail=1
    fi
else
    echo "  skipped: need both mawk and gawk installed"
fi

echo
if [ $fail = 0 ]; then
    echo "$checked checks passed"
else
    echo "FAILED"
fi
exit $fail
