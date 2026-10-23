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

mutant() { # FILE OLD NEW CHECK [root]
    [ "${5:-}" = root ] && [ "$(id -u)" != 0 ] && { echo "  skip (needs root) $4"; return; }
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
mutant copy.c 'renameat2(p->dfd, p->tmp, p->dfd, cand, RENAME_NOREPLACE)' \
    'renameat(p->dfd, p->tmp, p->dfd, cand)' \
    "...and neither earlier file was touched"
mutant copy.c 'if (n == 1) snprintf(suf, sizeof suf, " (from Windows)");' \
    'if (n >= 1) snprintf(suf, sizeof suf, " (from Windows)");' \
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
    "a user-dirs.dirs pointing out of her home is not obeyed (absolute)"
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
mutant ferry '        exec 3>&1 >>"$FERRY_STATE/ferry.log" 2>&1' '        exec 3>&1' \
    "--plain prints nothing but plain sentences"
mutant ferry-files '"Windows kept this folder on another drive' '"the registry redirects this folder to another drive' \
    "...and has no jargon in it"

# Root: the escalation the lead's review named.
mutant copy.c 'if (setgroups(1, &gg) || setresgid(g, g, g) || setresuid(u, u, u))' \
    'if (0)' "nothing was written through her symlinks into a root-owned folder" root
mutant copy.c '        die("will not write into a home as root (pass --as UID:GID)");' '        ;' \
    "ferry-copy refuses to write as root without --as (exit 1)" root

echo
printf 'mutants: %d caught, %d missed\n' "$caught" "$missed"
[ "$missed" -eq 0 ]
