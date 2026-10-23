# handovertest -- the Windows half of "Put Windows back with her files", on real Windows.
#
# AurOS leaves her files in a small exFAT partition (label AUROSHANDOV; layout in
# docs/FERRY.md, "The handover volume") and removes itself. At the next start of Windows the
# put-back task (SYSTEM) runs src/aurbridge/putback.ps1: it validates what AurOS left, hands
# the copying to a task that runs AS HER, then reads her copies, checks every one against the
# hash AurOS wrote, and only then removes the partition and gives drive C: its room back.
# What AurOS wrote is UNTRUSTED (it may have been compromised), so most of this file is
# hostile input.
#
# This builds that disk -- a throwaway VHDX with a stand-in C: (NTFS, shrunk), the handover
# partition filled the way AurOS fills it, a gap, and a recovery-type partition at the end --
# and runs the SHIPPED script (written out by `aurbridge.exe putback-script`) against it:
#
#   happy       system pass, user pass, system pass: every file arrives byte for byte; a name
#               already taken keeps both; Read me.txt is not overwritten; the partition goes;
#               C: is its size again; the file system is clean; a third pass changes nothing
#   cut-copy    the user pass stopped dead after two files, run again: the same end
#   hostile     a list with "..", a drive, a stream, a backslash, a rooted name, a folder that
#               is not hers, a reserved name, a trailing dot, a user name that is a path:
#               nothing is copied, nothing is written outside her folder, nothing removed
#   links       a junction planted at "From AurOS" / in it: the user pass writes nothing
#               through it; a forged job file is not obeyed
#   forged      the user's report says "all saved" and nothing is: the partition stays
#   tampered    one byte of one file changed after AurOS wrote DONE: no bad copy is left, the
#               partition is NOT removed
#   no-done     no DONE file: nothing is copied, nothing removed
#   no-room     Windows has too little free space and cannot take more: nothing removed
#   bitlocker   the happy path again with the stand-in C: encrypted (if this runner can)
#
#   pwsh tools/handovertest.ps1 -Exe path\to\aurbridge.exe
# With -Script, tests that file (a quick run needs no build); without it, the script
# the installer carries, written out by the exe.
param([string]$Exe = '', [string]$Script = '')
$ErrorActionPreference = 'Stop'
$work = Join-Path $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }) 'handover'
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
$script:fail = 0; $script:checks = 0
function Ok([string]$what, [bool]$cond, [string]$detail = '') {
    $script:checks++
    if ($cond) { '    {0,-76} ok' -f $what } else { $script:fail++; '    {0,-76} FAIL' -f $what; if ($detail) { "      $detail" } }
}
function Sha([string]$p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
function ShaBytes([byte[]]$b) { ([System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($b)) -replace '-', '').ToLowerInvariant() }
$me = "$env:USERDOMAIN\$env:USERNAME"

$PS1 = Join-Path $work 'putback.ps1'
if ($Script) { Copy-Item $Script $PS1 }
else { & $Exe putback-script $PS1 }
if (-not (Test-Path $PS1)) { throw 'no put-back script to test' }

# ---- the fixture: what AurOS would have put in the handover volume ------------------------------
$rng = [System.Random]::new(4242)
$Fixture = @(
    @('Documents', 'letter.txt', 3000), @('Documents', 'clash.txt', 100),
    @('Documents', 'Taxes/2024/return.pdf', 400000), @('Documents', 'empty.txt', 0),
    @('Pictures', 'Fotos de verano/caf' + [char]0xe9 + '.jpg', 2500000),
    @('Pictures', 'a.png', 70000), @('Music', 'song.mp3', 1800000),
    @('Videos', 'clip.mp4', 20000000), @('Desktop', 'notes.txt', 900), @('Downloads', 'setup.exe', 5000000)
)
$bytes = @{}
foreach ($f in $Fixture) { $b = [byte[]]::new($f[2]); $rng.NextBytes($b); $bytes[$f[0] + '/' + $f[1]] = $b }

# $rows: the fixture, or hostile ones (folder, rel, size, bytes). $user: the "#windows-user" line.
function New-Scenario([string]$name, [bool]$Done = $true, $Rows = $null, [string]$User = 'Bob') {
    $vhd = Join-Path $work "$name.vhdx"
    "create vdisk file=`"$vhd`" maximum=2048 type=expandable`nselect vdisk file=`"$vhd`"`nattach vdisk" |
        Set-Content "$work\dp.txt" -Encoding ASCII
    diskpart /s "$work\dp.txt" | Out-Null
    $n = (Get-DiskImage -ImagePath $vhd).Number
    $d = Get-Disk -Number $n
    if ($d.IsSystem -or $d.IsBoot -or $d.PartitionStyle -ne 'RAW') { throw "disk $n is not the new empty disk" }
    Initialize-Disk -Number $n -PartitionStyle GPT
    $p1 = @(New-Partition -DiskNumber $n -Size 700MB -AssignDriveLetter)[-1]     # (a reserved partition may come first)
    Format-Volume -Partition $p1 -FileSystem NTFS -NewFileSystemLabel WINSTAND -Confirm:$false | Out-Null
    $p2 = @(New-Partition -DiskNumber $n -Size 200MB)[-1]
    Set-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber -NoDefaultDriveLetter $true
    Format-Volume -Partition (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber) `
        -FileSystem exFAT -NewFileSystemLabel AUROSHANDOV -Confirm:$false | Out-Null
    $p3 = @(New-Partition -DiskNumber $n -Offset 1664MB -Size 256MB -GptType '{de94bba4-06d1-4d40-a16a-bfd50179d6ac}')[-1]
    $p1 = Get-Partition -DiskNumber $n -PartitionNumber $p1.PartitionNumber
    $p3 = Get-Partition -DiskNumber $n -PartitionNumber $p3.PartitionNumber
    $L = $p1.DriveLetter
    # The stand-in for C:: a user profile, and a file in it that is in the way.
    New-Item -ItemType Directory "${L}:\Users\Bob" -Force | Out-Null
    Set-Content "${L}:\Users\Bob\NTUSER.DAT" 'x'
    New-Item -ItemType Directory "${L}:\Users\Bob\From AurOS\Documents" -Force | Out-Null
    Set-Content "${L}:\Users\Bob\From AurOS\Documents\clash.txt" 'what was already in Windows'
    Set-Content "${L}:\Users\Bob\From AurOS\Read me.txt" 'mine'
    # The handover volume, filled as AurOS fills it.
    Add-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AssignDriveLetter
    $h = (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber).DriveLetter
    $root = "${h}:\AurOS-handover"
    New-Item -ItemType Directory $root | Out-Null
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText("$root\version", "1`n", $utf8)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("#ferry-handover`t1"); if ($User) { $lines.Add("#windows-user`t$User") }; $lines.Add("#aur-user`tuser")
    if (-not $Rows) { $Rows = $Fixture | ForEach-Object { , @($_[0], $_[1], $_[2], $bytes[$_[0] + '/' + $_[1]]) } }
    foreach ($f in $Rows) {
        $rel = $f[1]
        # The file goes in under a SAFE name on the volume; the list is what lies.
        $safe = "$root\files\" + $f[0] + '\' + (($rel -replace '[\\/:*?"<>|]', '_') -replace '\.\.', '__')
        New-Item -ItemType Directory (Split-Path -Parent $safe) -Force | Out-Null
        [System.IO.File]::WriteAllBytes($safe, $f[3])
        $lines.Add(($f[0], $rel, $f[2], (ShaBytes $f[3]), 1700000000) -join "`t")
    }
    [System.IO.File]::WriteAllText("$root\manifest.tsv", (($lines -join "`n") + "`n"), $utf8)
    [System.IO.File]::WriteAllText("$root\manifest.sha256", ((Sha "$root\manifest.tsv") + "  manifest.tsv`n"), $utf8)
    if ($Done) { [System.IO.File]::WriteAllText("$root\DONE", "ok`nfiles=$($Rows.Count)`nbytes=0`nat=now`n", $utf8) }
    Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AccessPath "${h}:\"
    $orig = $p3.Offset - $p1.Offset - 2MB     # a little slack for alignment
    $sd = Join-Path $work $name
    New-Item -ItemType Directory $sd | Out-Null
    "      layout: " + ((Get-Partition -DiskNumber $n | ForEach-Object { "#$($_.PartitionNumber) $($_.Type) off=$($_.Offset) size=$($_.Size)" }) -join '; ')
    $cfg = @{ disk_guid = $d.Guid; win_offset = $p1.Offset; shrunk = $p1.Size; orig = $orig;
              hib_file_on = 0; hiberboot = -1; task = 'AurOS put-back-test' } | ConvertTo-Json
    $params = Join-Path $sd 'putback.json'
    Set-Content $params $cfg -Encoding ASCII
    [pscustomobject]@{ Name = $name; Vhd = $vhd; Disk = $n; Letter = $L; Params = $params; State = $sd; Udir = (Join-Path $sd 'user');
                       Orig = $orig; P1 = $p1.PartitionNumber; Rows = $Rows }
}
function Remove-Scenario($s) {
    Dismount-DiskImage -ImagePath $s.Vhd -ErrorAction SilentlyContinue | Out-Null
    Remove-Item $s.Vhd -Force -ErrorAction SilentlyContinue
}
function Run-Ps([string[]]$a) {
    $script:out = (& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PS1 @a 2>&1 | Out-String)
    $script:rc = $LASTEXITCODE
    $script:rc
}
function Run-System($s, [string[]]$more = @()) {
    Run-Ps (@('-Mode', 'system', '-Params', $s.Params, '-Once', '-TestKeepTask', '-TestAccount', $me) + $more)
}
function Run-User($s, [string[]]$more = @()) {
    Run-Ps (@('-Mode', 'user', '-Job', (Join-Path $s.Udir 'job.json'), '-TestAnyProfile', '-NoWindow') + $more)
}
function Why($s) {
    $t = ''
    foreach ($l in 'putback.log') { $f = Join-Path $s.State $l; if (Test-Path $f) { $t += (Get-Content $f -Tail 3) -join ' | ' } }
    $f = Join-Path $s.Udir 'savefiles.log'; if (Test-Path $f) { $t += ' || ' + ((Get-Content $f -Tail 3) -join ' | ') }
    "$t $script:out".Trim()
}
function Handover-Present($s) {
    foreach ($p in (Get-Partition -DiskNumber $s.Disk)) {
        $v = $p | Get-Volume -ErrorAction SilentlyContinue
        if ($v -and $v.FileSystemLabel -eq 'AUROSHANDOV') { return $true }
    }
    $false
}
function Check-Arrived($s, [string]$label) {
    $into = "$($s.Letter):\Users\Bob\From AurOS"
    $bad = @()
    foreach ($f in $Fixture) {
        $rel = $f[1] -replace '/', '\'
        $dest = "$into\$($f[0])\$rel"
        if ($f[1] -eq 'clash.txt') { $dest = "$into\Documents\clash (from AurOS).txt" }
        if (-not (Test-Path -LiteralPath $dest)) { $bad += "missing $dest"; continue }
        if ((Sha $dest) -ne (ShaBytes $bytes[$f[0] + '/' + $f[1]])) { $bad += "different $dest" }
    }
    Ok "$label every file arrived, byte for byte" ($bad.Count -eq 0) ($bad -join '; ')
    Ok "$label a name already taken kept both files" ((Get-Content -Raw "$into\Documents\clash.txt").StartsWith('what was already in Windows'))
    Ok "$label her Read me.txt was not overwritten" ((Get-Content -Raw "$into\Read me.txt").StartsWith('mine'))
}
function Check-Finished($s, [string]$label) {
    Ok "$label the handover partition is gone" (-not (Handover-Present $s))
    $p1 = Get-Partition -DiskNumber $s.Disk -PartitionNumber $s.P1
    Ok "$label drive C:'s stand-in has its room back ($($p1.Size) of $($s.Orig))" ($p1.Size -ge ($s.Orig - 1MB))
    $r = Repair-Volume -DriveLetter $s.Letter -Scan
    Ok "$label the file system is clean" ("$r" -eq 'NoErrorsFound') "$r"
}
function Nothing-Outside($s, [string]$label) {
    # Nothing but the stand-in's own folders, her profile's From AurOS, and our filler.
    $stray = @(Get-ChildItem -Path "$($s.Letter):\" -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notin 'Users', '$RECYCLE.BIN', 'System Volume Information', 'elsewhere' })
    Ok "$label nothing was written outside her folder" ($stray.Count -eq 0) (($stray | ForEach-Object { $_.FullName }) -join ', ')
}

# ---- happy ---------------------------------------------------------------------------------------
'  happy'
$s = New-Scenario happy
$rc = Run-System $s
Ok 'the first system pass hands the job to her (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Ok '...and the partition is still there, nothing copied yet' ((Handover-Present $s) -and -not (Test-Path "$($s.Letter):\Users\Bob\From AurOS\Documents\letter.txt"))
Ok '...and there is a job for her to do' (Test-Path (Join-Path $s.Udir 'job.json'))
$rc = Run-User $s
Ok 'her pass saves them (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s 'after her pass:'
Ok '...the partition is still there until SYSTEM has checked' (Handover-Present $s)
$rc = Run-System $s
Ok 'the second system pass finishes (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s ''; Check-Finished $s ''
Ok 'the status says it is finished' ((Get-Content -Raw (Join-Path $s.Udir 'status.json')) -match 'finished')
$rc = Run-System $s
Ok 'a third pass changes nothing (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Finished $s 'after a third pass:'
Remove-Scenario $s

# ---- cut during her copy ----------------------------------------------------------------------
'  cut during her copy'
$s = New-Scenario cutcopy
$null = Run-System $s
$rc = Run-User $s @('-TestDieAfter', '2')
Ok 'stopped dead after two files' ($rc -eq 97) "exit $rc; $(Why $s)"
$rc = Run-System $s
Ok 'a system pass in the middle leaves the partition (exit 0)' ($rc -eq 0 -and (Handover-Present $s)) "exit $rc; $(Why $s)"
$rc = Run-User $s
Ok 'run again: she finishes' ($rc -eq 0) "exit $rc; $(Why $s)"
$rc = Run-System $s
Ok 'run again: SYSTEM finishes' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s 'after the cut:'; Check-Finished $s 'after the cut:'
Remove-Scenario $s

# ---- hostile lists ------------------------------------------------------------------------------
'  a list that AurOS did not write'
$good = @('Documents', 'ok.txt', 5, [byte[]](1, 2, 3, 4, 5))
function Hostile([string]$name, $row, [string]$User = 'Bob') {
    $s = New-Scenario ("h-" + $name) $true @(, $row) $User
    $rc = Run-System $s
    Ok "$name`: refused before anything is copied (exit 0)" ($rc -eq 0) "exit $rc; $(Why $s)"
    Ok "$name`: no job was made for her" (-not (Test-Path (Join-Path $s.Udir 'job.json')))
    Ok "$name`: the partition is NOT removed" (Handover-Present $s)
    Nothing-Outside $s "$name`:"
    Ok "$name`: ...and its status says the list is not AurOS's" ((Get-Content -Raw (Join-Path $s.Udir 'status.json')) -match 'not complete or not one AurOS would have written')
    Remove-Scenario $s
}
$evil = [byte[]](9, 9, 9)
Hostile 'dotdot'       @('Documents', '../../../../evil.txt', 3, $evil)
Hostile 'backslash'    @('Documents', '..\..\..\..\evil.txt', 3, $evil)
Hostile 'drive'        @('Documents', 'C:\evil.txt', 3, $evil)
Hostile 'stream'       @('Documents', 'letter.txt:evil', 3, $evil)
Hostile 'rooted'       @('Documents', '/evil.txt', 3, $evil)
Hostile 'folder'       @('Windows', 'evil.txt', 3, $evil)
Hostile 'device'       @('Documents', 'CON.txt', 3, $evil)
Hostile 'device2'      @('Documents', 'sub/nul', 3, $evil)
Hostile 'trailingdot'  @('Documents', 'evil.', 3, $evil)
Hostile 'emptyseg'     @('Documents', 'a//b.txt', 3, $evil)
Hostile 'dotseg'       @('Documents', 'a/./b.txt', 3, $evil)
Hostile 'username'     $good '..\..\Windows'
Hostile 'username2'    $good 'Bob\..\..'

# ---- links --------------------------------------------------------------------------------------------
'  links planted in her profile'
$s = New-Scenario links
New-Item -ItemType Directory "$($s.Letter):\elsewhere" | Out-Null
Remove-Item "$($s.Letter):\Users\Bob\From AurOS" -Recurse -Force
New-Item -ItemType Junction -Path "$($s.Letter):\Users\Bob\From AurOS" -Target "$($s.Letter):\elsewhere" | Out-Null
$null = Run-System $s
$rc = Run-User $s
Ok 'a junction at From AurOS: her pass writes nothing through it (exit 1)' ($rc -eq 1) "exit $rc; $(Why $s)"
Ok '...the junction target is empty' (@(Get-ChildItem "$($s.Letter):\elsewhere" -Force).Count -eq 0)
$null = Run-System $s
Ok '...the partition is NOT removed' (Handover-Present $s)
Remove-Item "$($s.Letter):\Users\Bob\From AurOS" -Force            # the junction itself
New-Item -ItemType Directory "$($s.Letter):\Users\Bob\From AurOS" | Out-Null
New-Item -ItemType Junction -Path "$($s.Letter):\Users\Bob\From AurOS\Documents" -Target "$($s.Letter):\elsewhere" | Out-Null
$rc = Run-User $s
Ok 'a junction at From AurOS\Documents: refused for those files (exit 1)' ($rc -eq 1) "exit $rc; $(Why $s)"
Ok '...the junction target is still empty' (@(Get-ChildItem "$($s.Letter):\elsewhere" -Force).Count -eq 0)
Ok '...and the files for other folders were saved' (Test-Path -LiteralPath "$($s.Letter):\Users\Bob\From AurOS\Music\song.mp3")
# a forged job file: the destination is somewhere else
$j = Get-Content -Raw (Join-Path $s.Udir 'job.json') | ConvertFrom-Json
$j.into = "$($s.Letter):\elsewhere"
$j | ConvertTo-Json | Set-Content (Join-Path $s.Udir 'job.json') -Encoding ASCII
$rc = Run-User $s
Ok 'a job file that names another destination is not obeyed (exit 1)' ($rc -eq 1) "exit $rc; $(Why $s)"
Ok '...nothing was written there' (@(Get-ChildItem "$($s.Letter):\elsewhere" -Force).Count -eq 0)
Remove-Scenario $s

# ---- the user's report is not believed ---------------------------------------------------------------
'  a report of "all saved" that is not true'
$s = New-Scenario forged
$null = Run-System $s
$keys = $Fixture | ForEach-Object { $_[0] + '/' + $_[1] }
# (a) claims for files that are not there
($keys | ForEach-Object { "$_`t$($s.Letter):\Users\Bob\From AurOS\$($_ -replace '/', '\')`tok" }) -join "`n" |
    Set-Content (Join-Path $s.Udir 'saved.tsv') -Encoding ASCII
$rc = Run-System $s
Ok 'files that are not there: the partition stays (exit 0)' ($rc -eq 0 -and (Handover-Present $s)) "exit $rc; $(Why $s)"
# (b) files that are there with the wrong bytes
foreach ($k in $keys) {
    $d = "$($s.Letter):\Users\Bob\From AurOS\$($k -replace '/', '\')"
    New-Item -ItemType Directory (Split-Path -Parent $d) -Force | Out-Null
    if ($k -ne 'Documents/clash.txt') { Set-Content -LiteralPath $d 'decoy' }
}
$rc = Run-System $s
Ok 'files with the wrong bytes: the partition stays (exit 0)' ($rc -eq 0 -and (Handover-Present $s)) "exit $rc; $(Why $s)"
Remove-Scenario $s

# ---- a changed file ---------------------------------------------------------------------------------
'  a file changed after AurOS wrote DONE'
$s = New-Scenario tampered
$n = $s.Disk; $p2 = (Get-Partition -DiskNumber $n | Where-Object { ($_ | Get-Volume -ErrorAction SilentlyContinue).FileSystemLabel -eq 'AUROSHANDOV' })
Add-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AssignDriveLetter
$h = (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber).DriveLetter
$victim = "${h}:\AurOS-handover\files\Documents\letter.txt"
$b = [System.IO.File]::ReadAllBytes($victim); $b[10] = $b[10] -bxor 1; [System.IO.File]::WriteAllBytes($victim, $b)
Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AccessPath "${h}:\"
$null = Run-System $s
$rc = Run-User $s
Ok 'her pass stops on it (exit 1)' ($rc -eq 1) "exit $rc; $(Why $s)"
Ok 'the bad copy was not left in Windows' (-not (Test-Path -LiteralPath "$($s.Letter):\Users\Bob\From AurOS\Documents\letter.txt"))
$null = Run-System $s
Ok 'the partition is NOT removed' (Handover-Present $s)
Remove-Scenario $s

# ---- no DONE ------------------------------------------------------------------------------------------
'  no DONE'
$s = New-Scenario nodone $false
$rc = Run-System $s
Ok 'the script leaves it alone (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Ok 'the partition is NOT removed' (Handover-Present $s)
Ok 'no job for her' (-not (Test-Path (Join-Path $s.Udir 'job.json')))
Remove-Scenario $s

# ---- no room --------------------------------------------------------------------------------------------
'  not enough room in Windows'
$s = New-Scenario noroom
$free = (Get-PSDrive -Name $s.Letter).Free
fsutil file createnew "$($s.Letter):\filler.bin" ([Math]::Max(0, $free - 10MB)) | Out-Null
$rc = Run-System $s
Ok 'the script waits (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Ok 'the partition is NOT removed' (Handover-Present $s)
Ok 'it says it needs room' ((Get-Content -Raw (Join-Path $s.Udir 'status.json')) -match 'GB more free space')
Remove-Scenario $s

# ---- BitLocker ------------------------------------------------------------------------------------------
'  with the stand-in for C: BitLocker-encrypted'
if (Get-Command Enable-BitLocker -ErrorAction SilentlyContinue) {
    $s = New-Scenario bitlocker
    $pw = ConvertTo-SecureString 'AurOS-test-only-1234' -AsPlainText -Force
    Enable-BitLocker -MountPoint "$($s.Letter):" -PasswordProtector -Password $pw -UsedSpaceOnly -SkipHardwareTest | Out-Null
    for ($i = 0; $i -lt 120 -and (Get-BitLockerVolume "$($s.Letter):").VolumeStatus -ne 'FullyEncrypted'; $i++) { Start-Sleep 5 }
    Ok 'the stand-in is BitLocker-encrypted' ((Get-BitLockerVolume "$($s.Letter):").VolumeStatus -eq 'FullyEncrypted')
    $null = Run-System $s
    $rc = Run-User $s
    Ok 'her pass saves them (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
    $rc = Run-System $s
    Ok 'SYSTEM finishes (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
    Check-Arrived $s 'encrypted:'
    Ok 'encrypted: the handover partition is gone' (-not (Handover-Present $s))
    Ok 'encrypted: it is still encrypted' ((Get-BitLockerVolume "$($s.Letter):").VolumeStatus -eq 'FullyEncrypted')
    Remove-Scenario $s
} else {
    'SKIPPED: this runner has no BitLocker (the feature is not installed)'
}

''
if ($script:fail -gt 0) { "$script:fail of $script:checks wrong. Each is a person whose files did not reach Windows, went too soon, or let something write where it should not."; exit 1 }
"$script:checks checks: her files reach Windows, are checked there, and only then does anything go."
