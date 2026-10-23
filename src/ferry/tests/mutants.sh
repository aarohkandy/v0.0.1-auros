#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  Prove the checks bite. Each mutant puts ONE known bug back into a
#  copy of Ferry; the suite must then fail the check named beside it.
#  A check that stays green with its bug back is not a test.
#
#    sh tests/mutants.sh          (root-only mutants run when root:
#                                  use tests/asroot.sh's container)
# ═══════════════════════════════════════════════════════════════════
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
caught=0; missed=0

mutant() { # FILE OLD NEW CHECK [root|user]
    [ "${5:-}" = root ] && [ "$(id -u)" != 0 ] && { echo "  skip (needs root) $4"; return; }
    # As root her uid cannot write the outside folder either way.
    [ "${5:-}" = user ] && [ "$(id -u)" = 0 ] && { echo "  skip (as a user only) $4"; return; }
    W=$(mktemp -d "${TMPDIR:-/tmp}/ferrymut.XXXXXX")
    cp -r "$HERE"/. "$W"/
    rm -rf "$W/build"
    if ! python3 - "$W/$1" "$2" "$3" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if s.count(old) != 1:
    sys.exit("mutation does not apply exactly once")
open(p, "w").write(s.replace(old, new))
PY
    then missed=$((missed+1)); echo "  BROKEN MUTANT  $4"; rm -rf "$W"; return; fi
    if (cd "$W" && make -s >/dev/null 2>&1; sh tests/run.sh 2>&1) | grep -qF "FAIL $4"; then
        caught=$((caught+1)); echo "  caught  $4"
    else
        missed=$((missed+1)); echo "  MISSED  $4"
    fi
    rm -rf "$W"
}

# The OneDrive tag used to mean "not here": every file of a OneDrive
# Documents folder was skipped.
mutant winattr.c 'if (tag && tag_is_alias(tag)) return "reparse";' \
    'if (tag) return "placeholder";' \
    "a OneDrive file that IS on this computer comes across"
mutant copy.c 'if (suspect && !nonzero) {' 'if (0) {' \
    "a file of nothing but zeros and no blocks is not copied as a picture"
mutant copy.c 'renameat2(p->dfd, p->tmp, p->dfd, cand, RENAME_NOREPLACE) == 0) { placed = n; break; }' \
    'renameat(p->dfd, p->tmp, p->dfd, cand) == 0) { placed = n; break; }' \
    "...and neither earlier file was touched"
mutant copy.c 'if (n == 1) snprintf(suf, sizeof suf, " (from %s)", from_word);' \
    'if (n >= 1) snprintf(suf, sizeof suf, " (from %s)", from_word);' \
    "a second collision gets '(from Windows 2)'"
mutant copy.c '#define NAME_LIMIT  255' '#define NAME_LIMIT  300' \
    "a name with no room for '(from Windows)' is shortened, not lost"
mutant copy.c "dst[i] = (src[i] == '\\t' || src[i] == '\\n' || src[i] == '\\r') ? ' ' : src[i];" \
    'dst[i] = src[i];' \
    "no ledger row is split by a tab or newline in a name"
mutant copy.c '    futimens(df, ts);' '    (void)ts;' \
    "the date a file was last changed comes with it"
mutant copy.c '            clean_temps(dfd);' '            ;' \
    "a temporary from a run that was cut off is gone"
mutant copy.c 'int pct = work_total > 0 ? (int)(work_done * 100 / work_total) : 100;' \
    'int pct = disp_total > 0 ? (int)(disp_done * 100 / disp_total) : 100;' \
    "progress is by bytes"
mutant copy.c 'O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (jobs[i].srcfd' 'O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (jobs[i].srcfd' \
    "...and nothing came through it"
mutant ferry-common.sh '..) return 1 ;; esac' '..) : ;; esac' \
    "a '..' in a registry folder does not reach outside the volume"
mutant ferry-common.sh '        [ -L "$_cur/$_c" ] && return 1' '        :' \
    "a junction inside a folder's path is not followed"
mutant ferry-common.sh '        *) _v="" ;;
    esac
    printf' '        *) : ;;
    esac
    printf' \
    "a user-dirs.dirs pointing out of her home is not obeyed (absolute)" user
mutant ferry-firefox 'for _n in $SKIP_FILES $SECRET_FILES; do' 'for _n in $SKIP_FILES; do' \
    "logins.json (saved passwords) NOT copied"
# The import in its real context (commit 2).
# (the check that never matched /proc/mounts' format, put back)
mutant ferry-common.sh 'is_ro_mount() {' \
    'is_ro_mount() { grep -q " $1 .*[(,]ro[,)]" "${FERRY_MOUNTS:-/proc/mounts}"; return; }
is_ro_mount_unused() {' \
    "a read-only mount is recognised as read-only"
mutant ferry 'SELF=$(dirname "$(readlink -f "$0")")' 'SELF=$(cd "$(dirname "$0")" && pwd)' \
    "ferry started through a link finds its own tools"
mutant ferry-common.sh 'FERRY_BIN=$(dirname "$(readlink -f "$0")" 2>/dev/null || echo /usr/lib/ferry)' \
    'FERRY_BIN=$(cd "$(dirname "$0")" 2>/dev/null && pwd || echo /usr/lib/ferry)' \
    "ferry started through a link finds its own tools"
mutant ferry '        2) [ "$RUN_RC" = 1 ] || RUN_RC=2 ;;' '        2) : ;;' \
    "some files online-only: exit 2"
mutant ferry 'if [ "$_name" = files ]; then RUN_RC=1;' 'if false; then RUN_RC=1;' \
    "her files could not be copied at all: exit 1, never 'partial'" root
mutant copy.c '    if (disp_total > 0 && came == 0 && !dry_run) return 1;' '    (void)came;' \
    "her files could not be copied at all: exit 1, never 'partial'" root

# Plain words (commit 3).
mutant ferry '        exec 3>&1 >>"$FERRY_STATE/ferry.log" 2>&1
        FERRY_PLAIN_FD=3; export FERRY_PLAIN_FD
        echo "=== ferry run $(now) ==="' '        exec 3>&1
        FERRY_PLAIN_FD=3; export FERRY_PLAIN_FD
        echo "=== ferry run $(now) ==="' \
    "--plain prints nothing but plain sentences"
mutant ferry-files '"Windows kept this folder on another drive' '"the registry redirects this folder to another drive' \
    "...and has no jargon in it"

# Her browser (commit 4).
mutant ffhash.c '#define K0 0xc3a5c85c97cb3127ULL' '#define K0 0xc3a5c85c97cb3128ULL' \
    "/usr/lib/firefox is 4F96D1932A9F858E, as the real Firefox calls it"
mutant ffhash.c '    uint64_t a = f64(s) * K1, b = f64(s + 8);' '    uint64_t a = f64(s) * K1 + 1, b = f64(s + 8);' \
    "/usr/lib/firefox is 4F96D1932A9F858E, as the real Firefox calls it"
mutant ffhash.c '    uint64_t a = f64(s) + (n + f64(s + n - 16)) * K0;' '    uint64_t a = f64(s) + (n + f64(s + n - 15)) * K0;' \
    "...a path of the longer kind (33-64 bytes) agrees too"
mutant ffhash.c '    if (n > 64) {' '    if (n > 640) {' \
    "...a path it cannot vouch for is refused (exit 2)"
mutant ferry-firefox "if (!hasinst) printf " "if (0) printf " \
    "Firefox is told to open her Windows profile ([Install<name>] Default=)"
mutant ferry-firefox 'if (sec ~ /^Profile[0-9]+$/) { k = substr(sec, 8) + 0; if (k + 1 > nxt) nxt = k + 1 }' \
    'if (sec ~ /^Profile[0-9]+$/) { k = substr(sec, 8) + 0; if (k + 2 > nxt) nxt = k + 2 }' \
    "an existing profiles.ini: ours is [Profile2], numbering stays unbroken (Firefox stops at a gap)"
mutant ferry-firefox 'if [ -d "$FERRY_HOME/.config/mozilla/firefox" ]; then' 'if false; then' \
    "where Firefox keeps its profiles in ~/.config/mozilla/firefox, that is where it goes"
mutant ferry-firefox 'elif ff_used_here; then _switch=0;' 'elif false; then _switch=0;' \
    "after she used Firefox here, it is NOT switched away from her profile"
mutant ferry-firefox 'if ff_running; then _switch=0;' 'if false; then _switch=0;' \
    "while Firefox is open, its profiles.ini is left alone"
mutant ferry-firefox '    case " $_ours fromwindows.default-release " in *" ${_cur##*/} "*) return 1 ;; esac' '    :' \
    "bringing it across again changes nothing in profiles.ini (idempotent)"
mutant ferry-firefox 'elif [ -z "$FF_HASH" ]; then _switch=0;' 'elif false; then _switch=0;' \
    "with no Firefox installed, no profiles.ini is invented"
mutant chromemarks.c '        if (!strncasecmp(url, ok[i], strlen(ok[i]))) return 1;' '        return 1;' \
    "...a javascript: bookmark (a program) does not come"
mutant chromemarks.c "        case '<': fputs(\"&lt;\", stdout); break;" '' \
    "...a title that is markup is escaped"
mutant chromemarks.c '        if (++depth > MAX_DEPTH) goto bad;' '        ++depth;' \
    "200,000 nested brackets: refused, not a crash (exit 2)"

# Her browser (commit 4).
mutant ferry-firefox '        sec ~ /^\[Install/ && /^Default=/ { $0 = "Default=" rel }' '' \
    "after Firefox ran once here, it is switched to the profile from Windows"
mutant ferry-firefox '_running=0
ff_running && _running=1' '_running=0' \
    "while Firefox is open, its profiles.ini is left alone"
mutant chromemarks.c '        if (!strncasecmp(url, ok[i], strlen(ok[i]))) return 1;' '        return 1;' \
    "...a javascript: bookmark (a program) does not come"
mutant chromemarks.c "        case '<': fputs(\"&lt;\", stdout); break;" '' \
    "...a title that is markup is escaped"
mutant chromemarks.c '        if (++depth > MAX_DEPTH) goto bad;' '        ++depth;' \
    "200,000 nested brackets: refused, not a crash (exit 2)"

# The way back (her AurOS files into Windows).
mutant copy.c '"<>:\"/\\|?*"' '"<>\"/\\|?*"' \
    "a colon becomes an underscore"
mutant copy.c '        have = nreg_get(R, low);
        if (have) {' '        have = NULL;
        if (have) {' \
    "...hers lands beside it"
mutant copy.c '    if (!r->cap) return NULL;' '    return NULL;
    if (!r->cap) return NULL;' \
    "Report.docx and report.docx are kept apart (Windows sees them as one name)"
mutant copy.c '            if (bad && o + 1 <= NAME_LIMIT)' '            if (0)' \
    "CON is a device name in Windows: it is saved as _CON.txt"
mutant copy.c '        if (free_b >= 0 && free_b < want) {' '        if (0) {' \
    "not enough room in Windows: exit 4"
mutant copy.c '                cls = S_ISREG(st.st_mode) ? "hydrated" : S_ISLNK(st.st_mode) ? "link" : "special";' \
    '                cls = "hydrated";' \
    "a shortcut (symlink) is not followed: nothing from /etc came"
mutant ferry-back "--skip .~lock.*# " "" "an editor's lock file is not hers"
mutant ferry-back "    case \"\$RC\" in 0|2|4) : ;; *) stage_fail back \"ferry-copy exit \$RC\"; RC=1 ;; esac" "    :" \
    "not enough room in Windows: exit 4"
mutant ferry-back 'require_clean_volume "$FERRY_ROOT" "$FERRY_DEV"' ':' \
    "a hibernated Windows is refused (exit 3)"
mutant ferry 'signal_done back "$_rc" back' ':' \
    "the completion signal says: back, ok, and how many files"
mutant ferry 'signal_done import "$_rc" files firefox' ':' \
    "the completion signal says: import, partial, and how many files"
mutant ferry-common.sh '        5) _res=unopened ;; *) _res=failed ;;' '        5) _res=unopened ;; *) _res=ok ;;' \
    "the completion signal says: back, ok, and how many files"

mutant ffhash.c '        char hex[24];
        snprintf(hex, sizeof hex, "%016llX", (unsigned long long)cityhash64(w, n));
        puts(hex);' '        printf("%016llX\n", (unsigned long long)cityhash64(w, n));' \
    "every compiled helper carries the hardening the image build demands (PIE, RELRO, stack protector...)"

# Root: the escalation the lead's review named.
# Without the switch to her, the self-check refuses to copy at all...
mutant copy.c 'if (setgroups(1, &gg) || setresgid(g, g, g) || setresuid(u, u, u))' \
    'if (0)' "her files did come, and every one of them is hers" root
# ...and without the self-check too, root writes through her symlink.
mutant copy.c '            if (setgroups(1, &gg) || setresgid(g, g, g) || setresuid(u, u, u))
                die("could not become %u:%u: %s", u, g, strerror(errno));
            if (u != 0 && setuid(0) == 0) die("still root after dropping privileges");' \
    '            (void)gg;' "nothing was written through her symlinks into a root-owned folder" root
mutant copy.c '        die("will not write into a home as root (pass --as UID:GID)");' '        ;' \
    "ferry-copy refuses to write as root without --as (exit 1)" root

echo
printf 'mutants: %d caught, %d missed\n' "$caught" "$missed"
[ "$missed" -eq 0 ]
