# AurOS "put back" -- runs as SYSTEM at every start of Windows until it has finished
# (docs/AURBRIDGE.md, "Put Windows back with her files: the handover").
#
# Two jobs, in this order:
#   1. If AurOS left a handover partition (her AurOS files, copied there before AurOS
#      was removed), copy them into her Windows profile under "From AurOS" with Windows'
#      own drivers, check every copy against the hash AurOS wrote, and only then
#      remove the handover partition.
#   2. Give drive C: back the room AurOS used, put hibernation back as it was, and
#      unregister the task.
#
# Every step can be repeated: a power cut at any instant is picked up again at the next
# start. Nothing of hers is removed until it is verified in Windows.
#
# ASCII only: Windows PowerShell reads a file with no BOM as the ANSI code page.
param(
    [string]$Params = 'C:\ProgramData\AurOS\putback.json',
    # tests only: stop dead after this many files have been saved / just before the
    # handover partition would be removed. The task never passes these.
    [int]$TestDieAfter = 0,
    [string]$TestDieAt = '',
    [switch]$TestKeepTask
)
$ErrorActionPreference = 'Stop'
$cfg   = Get-Content -Raw -Path $Params | ConvertFrom-Json
$state = Split-Path -Parent $Params
$LOG   = Join-Path $state 'putback.log'
$STAT  = Join-Path $state 'status.json'
$LABEL = 'AUROSHANDOV'

function Log([string]$m) {
    try { ('{0:u} {1}' -f (Get-Date), $m) | Add-Content -Path $LOG -Encoding ASCII } catch {}
}
function Status([string]$phase, [string]$text, [uint64]$files, [uint64]$total, [uint64]$bytes, [uint64]$tbytes) {
    try {
        @{ phase = $phase; text = $text; files = $files; total = $total; bytes = $bytes;
           tbytes = $tbytes; at = (Get-Date).ToUniversalTime().ToString('o') } |
            ConvertTo-Json | Set-Content -Path $STAT -Encoding ASCII
    } catch {}
}
# Long names: every file call below uses the \\?\ form.
function LP([string]$p) { if ($p.StartsWith('\\?\')) { $p } else { '\\?\' + $p } }
function Append-Line([string]$path, [string]$line) {
    # No byte-order mark: other tools read this file too.
    [System.IO.File]::AppendAllText($path, $line + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

function Sha256File([string]$path) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    $s = [System.IO.File]::Open((LP $path), 'Open', 'Read', 'Read')
    try { $b = $h.ComputeHash($s) } finally { $s.Dispose(); $h.Dispose() }
    ([System.BitConverter]::ToString($b) -replace '-', '').ToLowerInvariant()
}

$disk = Get-Disk | Where-Object { $_.Guid -eq $cfg.disk_guid } | Select-Object -First 1
if (-not $disk) { exit 0 }
$win = Get-Partition -DiskNumber $disk.Number |
    Where-Object { $_.Offset -eq [uint64]$cfg.win_offset } | Select-Object -First 1
if (-not $win) { exit 0 }

# ---- 1. her files -------------------------------------------------------------------
function Find-Handover {
    foreach ($p in (Get-Partition -DiskNumber $disk.Number)) {
        if ($p.Type -ne 'Basic') { continue }
        $v = $p | Get-Volume -ErrorAction SilentlyContinue
        if ($v -and $v.FileSystemType -eq 'exFAT' -and $v.FileSystemLabel -eq $LABEL) { return $p }
    }
    return $null
}

function Save-Files($hp) {
    if (-not $hp.DriveLetter) {
        Add-PartitionAccessPath -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber -AssignDriveLetter
        $hp = Get-Partition -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber
    }
    $hroot = '{0}:\AurOS-handover' -f $hp.DriveLetter
    $man = Join-Path $hroot 'manifest.tsv'
    # Complete means DONE is there and the manifest is the one DONE was written for.
    if (-not (Test-Path (Join-Path $hroot 'DONE')) -or -not (Test-Path $man) -or
        -not (Test-Path (Join-Path $hroot 'manifest.sha256'))) {
        Log 'the handover volume is not complete (no DONE): left as it is'
        Status 'incomplete' 'AurOS left your files in a partition named AurOS-Handover, but not all of them were saved. Nothing has been removed.' 0 0 0 0
        return $false
    }
    $want = ((Get-Content -Path (Join-Path $hroot 'manifest.sha256') -TotalCount 1) -split '\s+')[0].ToLowerInvariant()
    if ((Sha256File $man) -ne $want) {
        Log 'manifest.tsv does not match manifest.sha256: left as it is'
        Status 'incomplete' 'The list of your files in AurOS-Handover is damaged. Nothing has been removed.' 0 0 0 0
        return $false
    }

    $rows = @(); $winuser = ''
    foreach ($line in [System.IO.File]::ReadAllLines($man, [System.Text.Encoding]::UTF8)) {
        if ($line.Length -eq 0) { continue }
        if ($line.StartsWith('#')) {
            $f = $line -split "`t"
            if ($f[0] -eq '#windows-user' -and $f.Count -ge 2) { $winuser = $f[1] }
            continue
        }
        $f = $line -split "`t"
        if ($f.Count -lt 5) { throw "a line of the list is not understood: $line" }
        $rows += [pscustomobject]@{ Folder = $f[0]; Rel = $f[1]; Size = [uint64]$f[2]; Sha = $f[3].ToLowerInvariant(); Mtime = [int64]$f[4] }
    }
    $total = [uint64]$rows.Count
    $tbytes = [uint64]0; foreach ($r in $rows) { $tbytes += $r.Size }

    # Whose folder. The user the files came from; else the only one on the computer;
    # else the shared folder.
    $sys = $win.DriveLetter
    $users = '{0}:\Users' -f $sys
    $prof = $null
    if ($winuser -and (Test-Path (Join-Path $users $winuser))) { $prof = Join-Path $users $winuser }
    if (-not $prof) {
        $c = @(Get-ChildItem -Path $users -Directory -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notin 'Public', 'Default', 'Default User', 'All Users', 'defaultuser0' -and
                           (Test-Path (Join-Path $_.FullName 'NTUSER.DAT')) })
        if ($c.Count -eq 1) { $prof = $c[0].FullName }
    }
    if (-not $prof) { $prof = Join-Path $users 'Public' }
    $into = Join-Path $prof 'From AurOS'

    # Room. C: was made smaller to hold AurOS; the room in front of the handover
    # partition is free now, so take what is needed from it first.
    $need = $tbytes + 256MB
    $free = (Get-PSDrive -Name $sys).Free
    if ($free -lt $need) {
        $m = (Get-PartitionSupportedSize -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).SizeMax
        if ($m -gt $win.Size) {
            Resize-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber -Size $m
            $free = (Get-PSDrive -Name $sys).Free
        }
    }
    if ($free -lt $need) {
        $gb = [Math]::Ceiling(($need - $free) / 1GB)
        Log "not enough room in Windows for her files (needs $need, has $free)"
        Status 'room' ("Your files from AurOS are safe, but Windows needs about $gb GB more free space to save them. Free some space; this will carry on by itself.") 0 $total 0 $tbytes
        return $false
    }

    $received = Join-Path $hroot 'received.tsv'
    $saved = @{}
    if (Test-Path $received) {
        foreach ($l in [System.IO.File]::ReadAllLines($received)) {
            $f = $l -split "`t"
            if ($f.Count -ge 4 -and $f[2] -eq 'ok') { $saved[$f[0]] = @{ Sha = $f[1]; Dest = $f[3] } }
        }
    }
    $done = [uint64]0; $dbytes = [uint64]0; $n = 0
    foreach ($r in $rows) {
        $key = '{0}/{1}' -f $r.Folder, $r.Rel
        $src = Join-Path $hroot ('files\' + $r.Folder + '\' + ($r.Rel -replace '/', '\'))
        $ok = $false
        if ($saved.ContainsKey($key) -and $saved[$key].Sha -eq $r.Sha -and [System.IO.File]::Exists((LP $saved[$key].Dest))) {
            # Resuming: believe it only if the copy still has that hash.
            if ((Sha256File $saved[$key].Dest) -eq $r.Sha) { $ok = $true }
        }
        if (-not $ok) {
            $dest = Join-Path $into ($r.Folder + '\' + ($r.Rel -replace '/', '\'))
            $dir = Split-Path -Parent $dest
            [void][System.IO.Directory]::CreateDirectory((LP $dir))
            # Never overwrite. A file already there with these very bytes is the file.
            $k = 1
            $leaf = Split-Path -Leaf $dest
            $stem = [System.IO.Path]::GetFileNameWithoutExtension($leaf)
            $ext = [System.IO.Path]::GetExtension($leaf)
            $cand = $dest
            while ([System.IO.File]::Exists((LP $cand)) -and (Sha256File $cand) -ne $r.Sha) {
                $tag = if ($k -eq 1) { ' (from AurOS)' } else { " (from AurOS $k)" }
                $cand = Join-Path $dir ($stem + $tag + $ext)
                $k++
            }
            if (-not [System.IO.File]::Exists((LP $cand))) {
                $part = $cand + '.aurpart'
                $h = [System.Security.Cryptography.SHA256]::Create()
                $in = [System.IO.File]::Open((LP $src), 'Open', 'Read', 'Read')
                $out = [System.IO.File]::Open((LP $part), 'Create', 'Write', 'None')
                try {
                    $buf = New-Object byte[] (4MB)
                    while (($got = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                        $out.Write($buf, 0, $got); [void]$h.TransformBlock($buf, 0, $got, $null, 0)
                    }
                    [void]$h.TransformFinalBlock($buf, 0, 0)
                    $out.Flush($true)
                } finally { $in.Dispose(); $out.Dispose() }
                $got = ([System.BitConverter]::ToString($h.Hash) -replace '-', '').ToLowerInvariant()
                $h.Dispose()
                if ($got -ne $r.Sha) {
                    [System.IO.File]::Delete((LP $part))
                    throw "the copy of $key does not match what AurOS wrote"
                }
                [System.IO.File]::Move((LP $part), (LP $cand))
                try { [System.IO.File]::SetLastWriteTimeUtc((LP $cand), [DateTimeOffset]::FromUnixTimeSeconds($r.Mtime).UtcDateTime) } catch {}
            }
            # Verified where it now lives, not where it came from.
            if ((Sha256File $cand) -ne $r.Sha) { throw "the saved copy of $key does not match what AurOS wrote" }
            Append-Line $received ($key + "`t" + $r.Sha + "`tok`t" + $cand)
            $saved[$key] = @{ Sha = $r.Sha; Dest = $cand }
        }
        $done++; $dbytes += $r.Size; $n++
        Status 'copying' ("Putting your files from AurOS into Windows: $done of $total files.") $done $total $dbytes $tbytes
        if ($TestDieAfter -gt 0 -and $n -ge $TestDieAfter) { Log "test: stopping after $n files"; exit 97 }
    }

    # Every row is in Windows and was checked THERE (just now, or on resuming, by its hash). Her note.
    try {
        $note = "These are the files you saved while you used AurOS.`r`nThey were copied here when you put Windows back.`r`n"
        [System.IO.File]::WriteAllText((LP (Join-Path $into 'Read me.txt')), $note)
    } catch {}
    Log "all $total files are saved and verified in Windows"
    Status 'saved' 'Your files from AurOS are in Windows, in the folder "From AurOS". Finishing up.' $total $total $tbytes $tbytes
    if ($TestDieAt -eq 'before-remove') { Log 'test: stopping before the removal'; exit 98 }

    Remove-Partition -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber -Confirm:$false
    Log 'the handover partition is removed'
    return $true
}

try {
$hp = Find-Handover
if ($hp) {
    if (-not (Save-Files $hp)) { exit 0 }
}

# ---- 2. the room, and hibernation, as they were ---------------------------------------
$w = [uint64]$cfg.orig
$s = [uint64]$cfg.shrunk
$win = Get-Partition -DiskNumber $disk.Number | Where-Object { $_.Offset -eq [uint64]$cfg.win_offset } | Select-Object -First 1
function Finish {
    if ($cfg.hib_file_on -eq 1) { & powercfg.exe /hibernate on }
    if ($cfg.hiberboot -ge 0) {
        Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -Value ([int]$cfg.hiberboot)
    }
    Status 'finished' 'Windows is back to the way it was.' 0 0 0 0
    if (-not $TestKeepTask) {
        Unregister-ScheduledTask -TaskName $cfg.task -Confirm:$false
        # Nothing left to run at the next start: take the script and its parameters away.
        Remove-Item -Path $Params, $PSCommandPath -Force -ErrorAction SilentlyContinue
    }
}
if ($win.Size -ge $w) { Finish; exit 0 }
if ($win.Size -lt $s) { exit 0 }
$m = (Get-PartitionSupportedSize -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).SizeMax
$t = [Math]::Min($w, $m)
if ($t -gt $win.Size) { Resize-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber -Size $t }
if ((Get-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).Size -ge $w) { Finish }
} catch {
    Log ('stopped: ' + $_.Exception.Message)
    Status 'error' 'AurOS could not finish putting your files in Windows. They are safe in the partition named AurOS-Handover, and this will try again the next time Windows starts.' 0 0 0 0
    exit 1
}
