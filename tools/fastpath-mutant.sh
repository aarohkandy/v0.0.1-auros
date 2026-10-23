#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  fastpath-mutant — break the fast path's Windows half on purpose
#
#  FIELD-MANUAL 6.2: a check that cannot fail is not a check. Each
#  mutant below breaks ONE thing in the source before build/aurbridge
#  runs (fastpath.yml, workflow_dispatch with `mutant=<name>`); the run
#  must then go red at the step named beside it. Never committed, never
#  shipped: it edits the checkout of a throwaway build job.
#
#    fence2-at-gap     the Windows-refuses-NTFS probe is aimed at the gap
#                      -> red: "Windows did not refuse a raw write inside NTFS"
#    no-window-check   plat_gap_write forgets the armed window
#                      -> red: "fence one let a write through" (free space
#                         that is not the gap)
#    no-arm-check      plat_gap_allow arms a window over a partition
#                      -> red: "a gap reaching into NTFS was armed"
#    gap-corrupt       one byte in the gap changes after the engine read
#                      it back -> red: the raw .NET read of the gap
#    undo-short        the undo grows C: back 1 MiB short
#                      -> red: "the partition is not its original size again"
#    no-guid-check     a disk whose GPT GUID differs is accepted
#                      -> red: "it ran on a disk it was not named"
#    no-sysdisk-check  the disk Windows runs from is accepted
#                      -> red: "refused, but not for being the system disk"
#    putback-no-hash   a copy is not checked against AurOS's hash (handover job)
#                      -> red: "a file changed after AurOS wrote DONE"
#    putback-no-done   a handover volume without DONE is consumed
#                      -> red: "no DONE"
#    putback-removes-first  SYSTEM removes the partition without waiting for her copies
#                      -> red: "the partition is still there until SYSTEM has checked"
#    putback-overwrites  a name already in Windows is not kept apart
#                      -> red: "a name already taken kept both files"
#    putback-no-validate  the list's names are not checked
#                      -> red: "a list that AurOS did not write" (every case)
#    putback-follows-links  a link in her profile is written through
#                      -> red: "links planted in her profile"
#    putback-trusts-job  her job file may name any destination
#                      -> red: "a job file that names another destination"
#    putback-trusts-report  SYSTEM believes the user's report without reading the copies
#                      -> red: "a report of all saved that is not true"
#    dir-follows-links the download folder may be a junction (SEC-010)
#                      -> red: "the folder was followed" (dircheck job)
#    dir-any-owner     a stranger's owner is trusted
#                      -> red: "owner S-1-5-32-545 should be trusted=0"
#    dir-open-acl      the folder lets Users in (SEC-001)
#                      -> red: "inherits permissions" / "somebody else can use it"
#    file-keeps-acl    a file moved in keeps the permissions it came with (SEC-010)
#                      -> red: "the moved-in file is still owned by" / "open to somebody else"
#    no-dll-harden     the installer searches its own folder for DLLs (SEC-003)
#                      -> red: "A PLANTED DLL WAS LOADED"
#    file-changed      (in the workflow, not here) one of her files changes
#                      -> red: "A FILE CHANGED"
# ═══════════════════════════════════════════════════════════════════
set -eu
cd "$(dirname "$0")/.."

sub() { # file  old  new   -- exactly one occurrence, or this mutant is void
    python3 - "$1" "$2" "$3" <<'EOPY'
import sys
p, old, new = sys.argv[1:]
s = open(p).read()
if s.count(old) != 1:
    sys.exit("mutant void: %r occurs %d times in %s" % (old, s.count(old), p))
open(p, 'w').write(s.replace(old, new))
EOPY
}

case "${1:-}" in
fence2-at-gap)
    sub src/aurbridge/main.c 'plat_gap_os_fence_probe(disk, L.win_first * ss' \
                             'plat_gap_os_fence_probe(disk, L.rsc_first * ss' ;;
no-window-check)
    sub src/aurbridge/plat_win.c 'if (off < g_gap_lo || last >= g_gap_hi) {' 'if (0) {' ;;
no-arm-check)
    sub src/aurbridge/plat_win.c 'if (lo <= parts[k].last_byte && parts[k].first_byte < hi) {' 'if (0) {' ;;
gap-corrupt)
    sub src/aurbridge/main.c '    printf("gapselftest readback=ok\n");' \
        '    { uint8_t x = 0x5A; plat_gap_write(disk, L.root_first * ss + 4096, &x, 1, why, sizeof why); plat_gap_flush(disk, why, sizeof why); }
    printf("gapselftest readback=ok\n");' ;;
undo-short)
    sub src/aurbridge/plat_win.c 'uint64_t want = to_bytes && to_bytes < smax ? to_bytes : smax;' \
                                 'uint64_t want = (to_bytes && to_bytes < smax ? to_bytes : smax) - (1u << 20);' ;;
dir-follows-links)
    sub src/aurbridge/plat_win.c '(at & FILE_ATTRIBUTE_REPARSE_POINT);' '0;' ;;
dir-any-owner)
    sub src/aurbridge/plat_win.c 'if (!owner || !IsValidSid(owner)) return 0;' 'if (1) return 1;' ;;
dir-open-acl)
    sub src/aurbridge/plat_win.c 'L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"' 'L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;BU)"' ;;
no-dll-harden)
    sub src/aurbridge/plat_win.c 'if (!f || !f(0x800)) SetDllDirectoryW(L"");' '(void)f;' ;;
file-keeps-acl)
    sub src/aurbridge/plat_win.c '            SetNamedSecurityInfoW(wt, SE_FILE_OBJECT,' '            if (0) SetNamedSecurityInfoW(wt, SE_FILE_OBJECT,' ;;
putback-no-hash)
    sub src/aurbridge/putback.ps1 'if ($gh -ne $r.Sha) {' 'if ($false) {' ;;
putback-no-done)
    sub src/aurbridge/putback.ps1 "if (-not (Test-Path (Join-Path \$hroot 'DONE')) -or -not (Test-Path \$man) -or" 'if ($false -or -not (Test-Path $man) -or' ;;
putback-removes-first)
    sub src/aurbridge/putback.ps1 'if (-not (All-Saved $m.Rows $prof)) {' 'if ($false) {' ;;
putback-overwrites)
    sub src/aurbridge/putback.ps1 'while ([System.IO.File]::Exists((LP $cand)) -and (Sha256File $cand) -ne $r.Sha) {' 'while ($false) {' ;;
putback-no-validate)
    sub src/aurbridge/putback.ps1 'foreach ($seg in $rel.Split('"'"'/'"'"')) { if (-not (Segment-Ok $seg)) { return $false } }' 'foreach ($seg in $rel.Split('"'"'/'"'"')) { }' ;;
putback-follows-links)
    sub src/aurbridge/putback.ps1 'if ($a -band [System.IO.FileAttributes]::ReparsePoint) { return $false }' 'if ($false) { return $false }' ;;
putback-trusts-job)
    sub src/aurbridge/putback.ps1 "if (\$into -ine (\$prof + '\\From AurOS')) { Log 'the destination is not From AurOS in the profile'; exit 1 }" '' ;;
putback-trusts-report)
    sub src/aurbridge/putback.ps1 'if ((Sha256File $dest) -ne $r.Sha) { continue }' '' ;;
no-guid-check)
    sub src/aurbridge/main.c 'if (_stricmp(g, want_guid) != 0) {' 'if (0) {' ;;
no-sysdisk-check)
    sub src/aurbridge/main.c 'r.disks[r.system_disk].index == disk)' 'r.disks[r.system_disk].index == disk && 0)' ;;
file-changed)
    # applied by fastpath.yml between the engine's run and the independent
    # check (it needs the throwaway disk); nothing to change in the source.
    echo "mutant file-changed is applied by the workflow"; exit 0 ;;
*)  echo "no such mutant: ${1:-}"; exit 2 ;;
esac
echo "mutant $1 applied"
git diff --stat
