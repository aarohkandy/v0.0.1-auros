# handovertest -- the Windows half of "Put Windows back with her files", on real Windows.
#
# AurOS leaves her files in a small exFAT partition (label AUROSHANDOV, layout:
# docs/AURBRIDGE.md and docs/handoff/streams/R.md) and removes itself. At the next start
# of Windows the put-back task runs src/aurbridge/putback.ps1, which copies them into
# her profile under "From AurOS", checks every copy against the hash AurOS wrote, and only
# then removes the partition and gives drive C: its room back.
#
# This builds that disk -- a throwaway VHDX with a stand-in C: (NTFS, shrunk), the
# handover partition filled the way AurOS fills it, a gap, and a recovery-type partition
# at the end -- and runs the SHIPPED script (written out by `aurbridge.exe putback-script`)
# against it, then breaks it on purpose:
#
#   happy       every file arrives, byte for byte; a name already taken becomes
#               "name (from AurOS)"; the handover partition goes; C: is its size again;
#               the file system is clean
#   cut-copy    stopped dead after two files, run again: the same end
#   cut-remove  stopped after every file is verified and before the partition is removed,
#               run again: the same end
#   tampered    one byte of one file changed after AurOS wrote DONE: the script stops, the
#               partition is NOT removed
#   no-done     no DONE file: nothing is copied, nothing removed
#   no-room     Windows has too little free space and cannot take more: nothing removed,
#               a message that says how much
#   bitlocker   the happy path again with the stand-in C: encrypted (if this runner can)
#
#   pwsh tools/handovertest.ps1 -Exe path\to\aurbridge.exe
param([Parameter(Mandatory)][string]$Exe)
$ErrorActionPreference = 'Stop'
$work = Join-Path $env:RUNNER_TEMP 'handover'
if (-not $env:RUNNER_TEMP) { $work = Join-Path $env:TEMP 'handover' }
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
$script:fail = 0; $script:checks = 0
function Ok([string]$what, [bool]$cond, [string]$detail = '') {
    $script:checks++
    if ($cond) { '    {0,-72} ok' -f $what } else { $script:fail++; '    {0,-72} FAIL' -f $what; if ($detail) { "      $detail" } }
}
function Sha([string]$p) { (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }

$PS1 = Join-Path $work 'putback.ps1'
& $Exe putback-script $PS1
if (-not (Test-Path $PS1)) { throw 'aurbridge.exe putback-script wrote nothing' }

# ---- the fixture: what AurOS would have put in the handover volume ---------------------
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

function New-Scenario([string]$name, [switch]$Done = $true) {
    $vhd = Join-Path $work "$name.vhdx"
    "create vdisk file=`"$vhd`" maximum=2048 type=expandable`nselect vdisk file=`"$vhd`"`nattach vdisk" |
        Set-Content "$work\dp.txt" -Encoding ASCII
    diskpart /s "$work\dp.txt" | Out-Null
    $n = (Get-DiskImage -ImagePath $vhd).Number
    $d = Get-Disk -Number $n
    if ($d.IsSystem -or $d.IsBoot -or $d.PartitionStyle -ne 'RAW') { throw "disk $n is not the new empty disk" }
    Initialize-Disk -Number $n -PartitionStyle GPT
    $p1 = New-Partition -DiskNumber $n -Size 700MB -AssignDriveLetter
    Format-Volume -Partition $p1 -FileSystem NTFS -NewFileSystemLabel WINSTAND -Confirm:$false | Out-Null
    $p2 = New-Partition -DiskNumber $n -Size 200MB
    Set-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber -NoDefaultDriveLetter $true
    Format-Volume -Partition (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber) `
        -FileSystem exFAT -NewFileSystemLabel AUROSHANDOV -Confirm:$false | Out-Null
    $p3 = New-Partition -DiskNumber $n -Offset 1664MB -Size 256MB -GptType '{de94bba4-06d1-4d40-a16a-bfd50179d6ac}'
    $p1 = Get-Partition -DiskNumber $n -PartitionNumber $p1.PartitionNumber
    $p3 = Get-Partition -DiskNumber $n -PartitionNumber $p3.PartitionNumber
    $L = $p1.DriveLetter
    # The stand-in for C:: a user profile, and a file in it that is in the way.
    New-Item -ItemType Directory "${L}:\Users\Bob" -Force | Out-Null
    Set-Content "${L}:\Users\Bob\NTUSER.DAT" 'x'
    New-Item -ItemType Directory "${L}:\Users\Bob\From AurOS\Documents" -Force | Out-Null
    Set-Content "${L}:\Users\Bob\From AurOS\Documents\clash.txt" 'what was already in Windows'
    # The handover volume, filled as AurOS fills it.
    Add-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AssignDriveLetter
    $h = (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber).DriveLetter
    $root = "${h}:\AurOS-handover"
    New-Item -ItemType Directory $root | Out-Null
    Set-Content "$root\version" '1' -Encoding ASCII
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('#ferry-handover 1'); $lines.Add("#windows-user`tBob"); $lines.Add("#aur-user`tuser")
    foreach ($f in $Fixture) {
        $rel = $f[1]; $dir = Split-Path -Parent ("$root\files\" + $f[0] + '\' + ($rel -replace '/', '\'))
        New-Item -ItemType Directory $dir -Force | Out-Null
        $p = "$root\files\" + $f[0] + '\' + ($rel -replace '/', '\')
        [System.IO.File]::WriteAllBytes($p, $bytes[$f[0] + '/' + $rel])
        $lines.Add(($f[0], $rel, $f[2], (Sha $p), 1700000000) -join "`t")
    }
    [System.IO.File]::WriteAllText("$root\manifest.tsv", (($lines -join "`n") + "`n"), $utf8)
    [System.IO.File]::WriteAllText("$root\manifest.sha256", ((Sha "$root\manifest.tsv") + "  manifest.tsv`n"), $utf8)
    if ($Done) { [System.IO.File]::WriteAllText("$root\DONE", "ok`nfiles=$($Fixture.Count)`nbytes=0`nat=now`n", $utf8) }
    Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AccessPath "${h}:\"
    $orig = $p3.Offset - $p1.Offset - 2MB     # a little slack for alignment
    $cfg = @{ disk_guid = $d.Guid; win_offset = $p1.Offset; shrunk = $p1.Size; orig = $orig;
              hib_file_on = 0; hiberboot = -1; task = 'AurOS put-back-test' } | ConvertTo-Json
    $params = Join-Path $work "$name.json"
    Set-Content $params $cfg -Encoding ASCII
    [pscustomobject]@{ Name = $name; Vhd = $vhd; Disk = $n; Letter = $L; Params = $params; Orig = $orig; P1 = $p1.PartitionNumber }
}
function Remove-Scenario($s) {
    Dismount-DiskImage -ImagePath $s.Vhd -ErrorAction SilentlyContinue | Out-Null
    Remove-Item $s.Vhd -Force -ErrorAction SilentlyContinue
}
function Run-Putback($s, [string[]]$more = @()) {
    $a = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $PS1, '-Params', $s.Params, '-TestKeepTask') + $more
    $script:out = (& powershell.exe @a 2>&1 | Out-String)
    $script:rc = $LASTEXITCODE
    $script:rc
}
function Why($s) {
    $l = Join-Path (Split-Path $s.Params) 'putback.log'
    $t = if (Test-Path $l) { (Get-Content $l -Tail 4) -join ' | ' } else { '' }
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
        $want = ([System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes[$f[0] + '/' + $f[1]])) -replace '-', '').ToLowerInvariant()
        if ((Sha $dest) -ne $want) { $bad += "different $dest" }
    }
    Ok "$label every file arrived, byte for byte" ($bad.Count -eq 0) ($bad -join '; ')
    Ok "$label a name already taken kept both files" ((Get-Content -Raw "$into\Documents\clash.txt").StartsWith('what was already in Windows'))
}
function Check-Finished($s, [string]$label) {
    Ok "$label the handover partition is gone" (-not (Handover-Present $s))
    $p1 = Get-Partition -DiskNumber $s.Disk -PartitionNumber $s.P1
    Ok "$label drive C:'s stand-in has its room back ($($p1.Size) of $($s.Orig))" ($p1.Size -ge ($s.Orig - 1MB))
    $r = Repair-Volume -DriveLetter $s.Letter -Scan
    Ok "$label the file system is clean" ("$r" -eq 'NoErrorsFound') "$r"
}

# ---- happy -----------------------------------------------------------------------------
'  happy'
$s = New-Scenario happy
$rc = Run-Putback $s
Ok 'the script finished (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s ''; Check-Finished $s ''
Ok 'the hand-written status says it is finished' ((Get-Content -Raw (Join-Path (Split-Path $s.Params) 'status.json')) -match 'finished')
Remove-Scenario $s

# ---- cut during the copy ----------------------------------------------------------------
'  cut during the copy'
$s = New-Scenario cutcopy
$rc = Run-Putback $s @('-TestDieAfter', '2')
Ok 'stopped dead after two files' ($rc -eq 97) "exit $rc; $(Why $s)"
Ok 'the handover partition is still there' (Handover-Present $s)
$rc = Run-Putback $s
Ok 'run again: finished' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s 'after the cut:'; Check-Finished $s 'after the cut:'
Remove-Scenario $s

# ---- cut before the removal ---------------------------------------------------------------
'  cut after the last check, before the removal'
$s = New-Scenario cutremove
$rc = Run-Putback $s @('-TestDieAt', 'before-remove')
Ok 'stopped dead before the removal' ($rc -eq 98) "exit $rc; $(Why $s)"
Ok 'the handover partition is still there' (Handover-Present $s)
Check-Arrived $s 'at the cut:'
$rc = Run-Putback $s
Ok 'run again: finished' ($rc -eq 0) "exit $rc; $(Why $s)"
Check-Arrived $s 'after the cut:'; Check-Finished $s 'after the cut:'
Remove-Scenario $s

# ---- a changed file -------------------------------------------------------------------------
'  a file changed after AurOS wrote DONE'
$s = New-Scenario tampered
$n = $s.Disk; $p2 = (Get-Partition -DiskNumber $n | Where-Object { ($_ | Get-Volume -ErrorAction SilentlyContinue).FileSystemLabel -eq 'AUROSHANDOV' })
Add-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AssignDriveLetter
$h = (Get-Partition -DiskNumber $n -PartitionNumber $p2.PartitionNumber).DriveLetter
$victim = "${h}:\AurOS-handover\files\Documents\letter.txt"
$b = [System.IO.File]::ReadAllBytes($victim); $b[10] = $b[10] -bxor 1; [System.IO.File]::WriteAllBytes($victim, $b)
Remove-PartitionAccessPath -DiskNumber $n -PartitionNumber $p2.PartitionNumber -AccessPath "${h}:\"
$rc = Run-Putback $s
Ok 'the script stops (exit 1)' ($rc -eq 1) "exit $rc; $(Why $s)"
Ok 'the handover partition is NOT removed' (Handover-Present $s)
Ok 'the bad copy was not left in Windows' (-not (Test-Path -LiteralPath "$($s.Letter):\Users\Bob\From AurOS\Documents\letter.txt"))
Remove-Scenario $s

# ---- no DONE ------------------------------------------------------------------------------
'  no DONE'
$s = New-Scenario nodone -Done:$false
$rc = Run-Putback $s
Ok 'the script leaves it alone (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Ok 'the handover partition is NOT removed' (Handover-Present $s)
Ok 'nothing was copied' (-not (Test-Path "$($s.Letter):\Users\Bob\From AurOS\Documents\letter.txt"))
Remove-Scenario $s

# ---- no room --------------------------------------------------------------------------------
'  not enough room in Windows'
$s = New-Scenario noroom
$free = (Get-PSDrive -Name $s.Letter).Free
$fill = [Math]::Max(0, $free - 10MB)
fsutil file createnew "$($s.Letter):\filler.bin" $fill | Out-Null
$rc = Run-Putback $s
Ok 'the script waits (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
Ok 'the handover partition is NOT removed' (Handover-Present $s)
Ok 'it says it needs room' ((Get-Content -Raw (Join-Path (Split-Path $s.Params) 'status.json')) -match 'GB more free space')
Remove-Scenario $s

# ---- BitLocker ------------------------------------------------------------------------------
'  with the stand-in for C: BitLocker-encrypted'
if (Get-Command Enable-BitLocker -ErrorAction SilentlyContinue) {
    $s = New-Scenario bitlocker
    $pw = ConvertTo-SecureString 'AurOS-test-only-1234' -AsPlainText -Force
    Enable-BitLocker -MountPoint "$($s.Letter):" -PasswordProtector -Password $pw -UsedSpaceOnly -SkipHardwareTest | Out-Null
    for ($i = 0; $i -lt 120 -and (Get-BitLockerVolume "$($s.Letter):").VolumeStatus -ne 'FullyEncrypted'; $i++) { Start-Sleep 5 }
    Ok 'the stand-in is BitLocker-encrypted' ((Get-BitLockerVolume "$($s.Letter):").VolumeStatus -eq 'FullyEncrypted')
    $rc = Run-Putback $s
    Ok 'the script finished (exit 0)' ($rc -eq 0) "exit $rc; $(Why $s)"
    Check-Arrived $s 'encrypted:'
    Ok 'encrypted: the handover partition is gone' (-not (Handover-Present $s))
    Ok 'encrypted: it is still encrypted' ((Get-BitLockerVolume "$($s.Letter):").VolumeStatus -eq 'FullyEncrypted')
    Remove-Scenario $s
} else {
    'SKIPPED: this runner has no BitLocker (the feature is not installed)'
}

''
if ($script:fail -gt 0) { "$script:fail of $script:checks wrong. Each is a person whose files did not reach Windows, or went too soon."; exit 1 }
"$script:checks checks: her files reach Windows, are checked there, and only then does anything go."
