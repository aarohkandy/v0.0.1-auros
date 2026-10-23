# AurOS "put back" (docs/AURBRIDGE.md, "Put Windows back with her files: the handover").
#
# ONE FILE, TWO PLACES. -Mode system runs as SYSTEM at every start of Windows until it has
# finished; -Mode user runs as THE USER at her logon (the same bytes, a copy in a folder she
# can read).
#
# What AurOS left on this disk is UNTRUSTED INPUT: a handover partition whose manifest and
# files were written by something running inside AurOS, which may have been compromised. So
# SYSTEM never writes into a user's profile:
#
#   system  checks the volume is complete (DONE, manifest hash), validates EVERY row of the
#           manifest, finds whose files they are, makes room in Windows, hands the job to a
#           task that runs as that user, waits, then READS her copies and checks every one
#           against the hash AurOS wrote. Only then does it remove the handover partition,
#           give drive C: back the room AurOS used, put hibernation back as it was, and
#           unregister the tasks.
#   user    copies the files into her own profile under "From AurOS" with her own rights
#           (so a link planted in her profile can steer nothing that she could not already
#           reach), never overwriting, never following a link, each copy checked by hash.
#
# Every step can be repeated: a power cut at any instant is picked up again at the next
# start or logon. Nothing of hers is removed until it is verified in Windows.
#
# ASCII only: Windows PowerShell reads a file with no BOM as the ANSI code page.
param(
    [ValidateSet('system', 'user')][string]$Mode = 'system',
    [string]$Params = 'C:\ProgramData\AurOS\putback.json',     # system
    [string]$Job = '',                                          # user
    # tests only: one pass instead of waiting; no per-user task; act for another account /
    # profile; no window. The tasks never pass these.
    [switch]$Once,
    [switch]$TestKeepTask,
    [string]$TestAccount = '',
    [switch]$TestAnyProfile,
    [switch]$NoWindow,
    [int]$TestDieAfter = 0
)
$ErrorActionPreference = 'Stop'
$LABEL = 'AUROSHANDOV'
$FOLDERS = @('Desktop', 'Documents', 'Downloads', 'Music', 'Pictures', 'Videos')
$RESERVED = '^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\..*)?$'

$state = if ($Mode -eq 'system') { Split-Path -Parent $Params } else { Split-Path -Parent $Job }
$LOG = Join-Path $state ($(if ($Mode -eq 'system') { 'putback.log' } else { 'savefiles.log' }))
$udir = if ($Mode -eq 'system') { Join-Path $state 'user' } else { $state }
function Log([string]$m) {
    try { ('{0:u} {1}' -f (Get-Date), $m) | Add-Content -Path $LOG -Encoding ASCII } catch {}
}
function Status([string]$phase, [string]$text) {
    try {
        [void](New-Item -ItemType Directory -Force $udir)
        @{ phase = $phase; text = $text; at = (Get-Date).ToUniversalTime().ToString('o') } |
            ConvertTo-Json | Set-Content -Path (Join-Path $udir 'status.json') -Encoding ASCII
    } catch {}
}
function LP([string]$p) { if ($p.StartsWith('\\?\')) { $p } else { '\\?\' + $p } }
function Sha256File([string]$path) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    $s = [System.IO.File]::Open((LP $path), 'Open', 'Read', 'Read')
    try { $b = $h.ComputeHash($s) } finally { $s.Dispose(); $h.Dispose() }
    ([System.BitConverter]::ToString($b) -replace '-', '').ToLowerInvariant()
}

# ---- what a name from the manifest may be ---------------------------------------------------
# One segment of a path: not empty, not dots-only, none of Windows' forbidden characters
# (':' also shuts out alternate data streams), no control characters, no trailing dot or
# space, no reserved device names.
function Segment-Ok([string]$s) {
    if ($s.Length -eq 0 -or $s.Length -gt 255) { return $false }
    if ($s -eq '.' -or $s -eq '..') { return $false }
    if ($s -match '[\\/:*?"<>|\x00-\x1f]') { return $false }
    if ($s.EndsWith('.') -or $s.EndsWith(' ')) { return $false }
    if ($s -match $RESERVED) { return $false }
    return $true
}
function Rel-Ok([string]$rel) {
    if ($rel.Length -eq 0 -or $rel.Length -gt 600 -or $rel.StartsWith('/')) { return $false }
    foreach ($seg in $rel.Split('/')) { if (-not (Segment-Ok $seg)) { return $false } }
    return $true
}
function User-Ok([string]$u) {
    return ($u -match '^[^\\/:*?"<>|.][^\\/:*?"<>|]*$') -and $u.Length -le 255 -and ($u -notmatch '[\x00-\x1f]')
}
# A path is "inside" a root if, fully resolved, it starts with the root and a separator.
function Inside([string]$path, [string]$root) {
    $r = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    $p = [System.IO.Path]::GetFullPath($path)
    return $p.StartsWith($r, [System.StringComparison]::OrdinalIgnoreCase)
}
# No component from $root down to $path (a file or a folder) may be a link of any kind.
function No-Links([string]$root, [string]$path) {
    $cur = [System.IO.Path]::GetFullPath($root).TrimEnd('\')
    $rest = [System.IO.Path]::GetFullPath($path).Substring($cur.Length).Trim('\')
    $chain = @($cur)
    foreach ($seg in $rest.Split('\')) { if ($seg) { $cur = $cur + '\' + $seg; $chain += $cur } }
    foreach ($c in $chain) {
        $a = $null
        try { $a = [System.IO.File]::GetAttributes((LP $c)) } catch { continue }   # not there yet
        if ($a -band [System.IO.FileAttributes]::ReparsePoint) { return $false }
    }
    return $true
}

# The manifest, every row validated. $null (and the reason logged) if ANY row is not what
# AurOS's own tools would write: a list that cannot be trusted is not partly trusted.
function Read-Manifest([string]$man) {
    $rows = New-Object System.Collections.Generic.List[object]
    $winuser = ''
    foreach ($line in [System.IO.File]::ReadAllLines($man, [System.Text.Encoding]::UTF8)) {
        if ($line.Length -eq 0) { continue }
        $f = $line -split "`t"
        if ($line.StartsWith('#')) {
            if ($f[0] -eq '#windows-user' -and $f.Count -ge 2) { $winuser = $f[1] }
            continue
        }
        if ($f.Count -ne 5) { Log 'bad list: a line with the wrong number of fields'; return $null }
        if ($FOLDERS -cnotcontains $f[0]) { Log "bad list: folder '$($f[0])'"; return $null }
        if (-not (Rel-Ok $f[1])) { Log "bad list: name '$($f[1])'"; return $null }
        if ($f[2] -notmatch '^[0-9]{1,15}$' -or $f[3] -notmatch '^[0-9a-fA-F]{64}$' -or $f[4] -notmatch '^[0-9]{1,12}$') {
            Log 'bad list: a size, hash or time that is not one'; return $null
        }
        $rows.Add([pscustomobject]@{ Folder = $f[0]; Rel = $f[1]; Size = [uint64]$f[2]; Sha = $f[3].ToLowerInvariant(); Mtime = [int64]$f[4] })
    }
    if ($winuser -and -not (User-Ok $winuser)) { Log "bad list: user '$winuser'"; return $null }
    return @{ Rows = $rows.ToArray(); User = $winuser }
}
function Manifest-Complete([string]$hroot) {
    $man = Join-Path $hroot 'manifest.tsv'
    if (-not (Test-Path (Join-Path $hroot 'DONE')) -or -not (Test-Path $man) -or
        -not (Test-Path (Join-Path $hroot 'manifest.sha256'))) { Log 'the handover volume is not complete (no DONE)'; return $null }
    $want = ((Get-Content -Path (Join-Path $hroot 'manifest.sha256') -TotalCount 1) -split '\s+')[0].ToLowerInvariant()
    if ((Sha256File $man) -ne $want) { Log 'manifest.tsv does not match manifest.sha256'; return $null }
    return Read-Manifest $man
}

# =====================================================================================
#  USER MODE: runs as her, at her logon
# =====================================================================================
if ($Mode -eq 'user') {
    try {
        $j = Get-Content -Raw -Path $Job | ConvertFrom-Json
        $prof = [System.IO.Path]::GetFullPath([string]$j.profile).TrimEnd('\')
        $into = [System.IO.Path]::GetFullPath([string]$j.into).TrimEnd('\')
        $hroot = [string]$j.hroot
        # Only ever her own profile, and only the one folder in it.
        if (-not $TestAnyProfile -and $prof -ine [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd('\')) { Log 'not this user''s profile: nothing done'; exit 0 }
        if ($into -ine ($prof + '\From AurOS')) { Log 'the destination is not From AurOS in the profile'; exit 1 }
        if ($hroot -notmatch '^[A-Za-z]:\\AurOS-handover$' -or -not (Test-Path $hroot)) { Log 'the handover volume is not there'; exit 0 }
        $m = Manifest-Complete $hroot
        if (-not $m) { exit 1 }
        $rows = $m.Rows
        $savedf = Join-Path $udir 'saved.tsv'
        $saved = @{}
        if (Test-Path $savedf) {
            foreach ($l in [System.IO.File]::ReadAllLines($savedf)) {
                $c = $l -split "`t"
                if ($c.Count -ge 3 -and $c[2] -eq 'ok') { $saved[$c[0]] = $c[1] }
            }
        }
        $win = $null
        if (-not $NoWindow) {
            try {
                Add-Type -AssemblyName System.Windows.Forms
                $form = New-Object System.Windows.Forms.Form
                $form.Text = 'AurOS'; $form.Width = 540; $form.Height = 170; $form.StartPosition = 'CenterScreen'
                $form.TopMost = $true; $form.ControlBox = $false
                $lab = New-Object System.Windows.Forms.Label
                $lab.Left = 16; $lab.Top = 16; $lab.Width = 490; $lab.Height = 56
                $lab.Font = New-Object System.Drawing.Font('Segoe UI', 11)
                $bar = New-Object System.Windows.Forms.ProgressBar
                $bar.Left = 16; $bar.Top = 84; $bar.Width = 490; $bar.Height = 22
                $form.Controls.Add($lab); $form.Controls.Add($bar); $form.Show()
                $win = @{ Form = $form; Label = $lab; Bar = $bar }
            } catch { $win = $null }
        }
        function Tick([string]$text, [int]$pct) {
            if ($win) { $win.Label.Text = $text; $win.Bar.Value = [Math]::Max(0, [Math]::Min(100, $pct)); [System.Windows.Forms.Application]::DoEvents() }
        }
        $done = 0; $blocked = 0; $n = 0
        foreach ($r in $rows) {
            $key = '{0}/{1}' -f $r.Folder, $r.Rel
            $rel = $r.Rel -replace '/', '\'
            $ok = $false
            if ($saved.ContainsKey($key)) {
                $d = $saved[$key]
                try {
                    if ((Inside $d $into) -and (No-Links $prof $d) -and [System.IO.File]::Exists((LP $d)) -and (Sha256File $d) -eq $r.Sha) { $ok = $true }
                } catch {}
            }
            if (-not $ok) {
                $dest = Join-Path $into ($r.Folder + '\' + $rel)
                $dir = Split-Path -Parent $dest
                if (-not (Inside $dest $into) -or -not (No-Links $prof $dest)) {
                    Log "refused (it would go through a link or outside From AurOS): $key"; $blocked++; continue
                }
                [void][System.IO.Directory]::CreateDirectory((LP $dir))
                if (-not (No-Links $prof $dir)) { Log "refused (a link): $key"; $blocked++; continue }
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
                    if ([System.IO.File]::Exists((LP $part))) { [System.IO.File]::Delete((LP $part)) }
                    $src = Join-Path $hroot ('files\' + $r.Folder + '\' + $rel)
                    $h = [System.Security.Cryptography.SHA256]::Create()
                    $in = [System.IO.File]::Open((LP $src), 'Open', 'Read', 'Read')
                    $out = [System.IO.File]::Open((LP $part), 'CreateNew', 'Write', 'None')
                    try {
                        $buf = New-Object byte[] (4MB)
                        while (($got = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                            $out.Write($buf, 0, $got); [void]$h.TransformBlock($buf, 0, $got, $null, 0)
                        }
                        [void]$h.TransformFinalBlock($buf, 0, 0)
                        $out.Flush($true)
                    } finally { $in.Dispose(); $out.Dispose() }
                    $gh = ([System.BitConverter]::ToString($h.Hash) -replace '-', '').ToLowerInvariant()
                    $h.Dispose()
                    if ($gh -ne $r.Sha) {
                        [System.IO.File]::Delete((LP $part))
                        Log "the copy of $key does not match what AurOS wrote"; $blocked++; continue
                    }
                    [System.IO.File]::Move((LP $part), (LP $cand))
                    try { [System.IO.File]::SetLastWriteTimeUtc((LP $cand), [DateTimeOffset]::FromUnixTimeSeconds($r.Mtime).UtcDateTime) } catch {}
                }
                # Verified where it now lives, not where it came from.
                if ((Sha256File $cand) -ne $r.Sha) { Log "the saved copy of $key does not match"; $blocked++; continue }
                [System.IO.File]::AppendAllText($savedf, ($key + "`t" + $cand + "`tok`n"), (New-Object System.Text.UTF8Encoding($false)))
                $saved[$key] = $cand
            }
            $done++; $n++
            Tick ("AurOS is putting your files in Windows: $done of $($rows.Count). Please leave this computer on.") ([int](100 * $done / [Math]::Max(1, $rows.Count)))
            Status 'copying' "Putting your files from AurOS into Windows: $done of $($rows.Count) files."
            if ($TestDieAfter -gt 0 -and $n -ge $TestDieAfter) { Log "test: stopping after $n files"; exit 97 }
        }
        $readme = Join-Path $into 'Read me.txt'
        if ($blocked -eq 0 -and -not [System.IO.File]::Exists((LP $readme))) {
            try { [System.IO.File]::WriteAllText((LP $readme), "These are the files you saved while you used AurOS.`r`nThey were copied here when you put Windows back.`r`n") } catch {}
        }
        if ($blocked -gt 0) {
            Tick ("$blocked of your files could not be saved safely. They are still safe in AurOS-Handover; please ask someone you trust to look.") 100
            Status 'blocked' "$blocked files could not be saved."
            if ($win) { Start-Sleep -Seconds 20 }
        } else {
            Tick 'Your files from AurOS are in Windows, in the folder "From AurOS".' 100
            Status 'saved' 'Your files from AurOS are in Windows, in the folder "From AurOS".'
            if ($win) { Start-Sleep -Seconds 8 }
        }
        if ($win) { $win.Form.Close() }
        exit $(if ($blocked -gt 0) { 1 } else { 0 })
    } catch {
        Log ('stopped: ' + $_.Exception.Message)
        exit 1
    }
}

# =====================================================================================
#  SYSTEM MODE
# =====================================================================================
$cfg = Get-Content -Raw -Path $Params | ConvertFrom-Json
$disk = Get-Disk | Where-Object { $_.Guid -eq $cfg.disk_guid } | Select-Object -First 1
if (-not $disk) { Log "no disk with GUID $($cfg.disk_guid)"; exit 0 }
$win = Get-Partition -DiskNumber $disk.Number |
    Where-Object { $_.Offset -eq [uint64]$cfg.win_offset } | Select-Object -First 1
if (-not $win) { Log "no partition at offset $($cfg.win_offset) on disk $($disk.Number)"; exit 0 }

function Find-Handover {
    foreach ($p in (Get-Partition -DiskNumber $disk.Number)) {
        if ($p.Type -ne 'Basic') { continue }
        $v = $p | Get-Volume -ErrorAction SilentlyContinue
        if ($v -and $v.FileSystemType -eq 'exFAT' -and $v.FileSystemLabel -eq $LABEL) { return $p }
    }
    return $null
}

# Whose files, and where: the list's user if that is an existing profile on this Windows,
# else the only profile there is. $null if there is none.
function Profile-For($m, [string]$sys) {
    $users = '{0}:\Users' -f $sys
    if ($m.User) {
        $cand = Join-Path $users $m.User
        if ((Inside $cand $users) -and (Test-Path -LiteralPath $cand -PathType Container) -and
            (No-Links $users $cand) -and (Test-Path (Join-Path $cand 'NTUSER.DAT'))) { return $cand }
    }
    $c = @(Get-ChildItem -Path $users -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notin 'Public', 'Default', 'Default User', 'All Users', 'defaultuser0' -and
                       -not ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -and
                       (Test-Path (Join-Path $_.FullName 'NTUSER.DAT')) })
    if ($c.Count -eq 1) { return $c[0].FullName }
    return $null
}

# Whose account runs the copy: the account whose profile this is, by the profile list.
function Account-For([string]$prof) {
    if ($TestAccount) { return $TestAccount }
    $pl = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList'
    foreach ($k in (Get-ChildItem $pl -ErrorAction SilentlyContinue)) {
        $path = (Get-ItemProperty -Path $k.PSPath -ErrorAction SilentlyContinue).ProfileImagePath
        if ($path -and ([System.IO.Path]::GetFullPath($path) -ieq [System.IO.Path]::GetFullPath($prof))) {
            try { return ([System.Security.Principal.SecurityIdentifier]$k.PSChildName).Translate([System.Security.Principal.NTAccount]).Value } catch {}
        }
    }
    return $null
}

# Every row of the list in her profile under "From AurOS", with the hash AurOS wrote -- each
# copy CHECKED here, by reading it; what the user task reports is only where to look.
$script:verified = @{}
function All-Saved($rows, [string]$prof) {
    $into = Join-Path $prof 'From AurOS'
    $saved = @{}
    $f = Join-Path $udir 'saved.tsv'
    if (Test-Path $f) {
        foreach ($l in [System.IO.File]::ReadAllLines($f)) {
            $c = $l -split "`t"
            if ($c.Count -ge 3 -and $c[2] -eq 'ok') { $saved[$c[0]] = $c[1] }
        }
    }
    $n = 0
    foreach ($r in $rows) {
        $key = '{0}/{1}' -f $r.Folder, $r.Rel
        if ($script:verified.ContainsKey($key)) { $n++; continue }
        if (-not $saved.ContainsKey($key)) { continue }
        $dest = $saved[$key]
        try {
            if (-not (Inside $dest $into) -or -not (No-Links $prof $dest)) { continue }
            if (-not [System.IO.File]::Exists((LP $dest))) { continue }
            if ((Sha256File $dest) -ne $r.Sha) { continue }
            $script:verified[$key] = $true
            $n++
        } catch { continue }
    }
    return ($n -eq $rows.Count)
}

try {
    [void](New-Item -ItemType Directory -Force $udir)
    $hp = Find-Handover
    if (-not $hp) { Log 'no handover partition' }
    if ($hp) {
        if (-not $hp.DriveLetter) {
            Add-PartitionAccessPath -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber -AssignDriveLetter
            $hp = Get-Partition -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber
        }
        $hroot = '{0}:\AurOS-handover' -f $hp.DriveLetter
        $m = Manifest-Complete $hroot
        if (-not $m) {
            Status 'incomplete' 'AurOS left your files in a partition named AurOS-Handover, but the list of them is not complete or not one AurOS would have written. Nothing has been copied or removed.'
            exit 0
        }
        $sys = $win.DriveLetter
        $prof = Profile-For $m $sys
        if (-not $prof) {
            Log 'no Windows user to give the files to'
            Status 'nouser' 'Your files from AurOS are safe, but Windows has no user to give them to. They stay in AurOS-Handover.'
            exit 0
        }
        if (-not (All-Saved $m.Rows $prof)) {
            # Room. C: was made smaller to hold AurOS; the room in front of the handover
            # partition is free now, so take what is needed from it first.
            $total = [uint64]0; foreach ($r in $m.Rows) { $total += $r.Size }
            $need = $total + 256MB
            $free = (Get-PSDrive -Name $sys).Free
            if ($free -lt $need) {
                $mx = (Get-PartitionSupportedSize -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).SizeMax
                if ($mx -gt $win.Size) {
                    Resize-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber -Size $mx
                    $free = (Get-PSDrive -Name $sys).Free
                }
            }
            if ($free -lt $need) {
                $gb = [Math]::Ceiling(($need - $free) / 1GB)
                Log "not enough room in Windows for her files (needs $need, has $free)"
                Status 'room' "Your files from AurOS are safe, but Windows needs about $gb GB more free space to save them. Free some space; this will carry on by itself."
                exit 0
            }
            # Hand the job to her.
            $account = Account-For $prof
            $jf = Join-Path $udir 'job.json'
            @{ hroot = $hroot; profile = $prof; into = (Join-Path $prof 'From AurOS') } | ConvertTo-Json | Set-Content -Path $jf -Encoding ASCII
            & icacls.exe $udir /grant $(if ($account) { '{0}:(OI)(CI)M' -f $account } else { '*S-1-5-32-545:(OI)(CI)M' }) | Out-Null
            if (-not $Once -and -not $TestKeepTask) {
                $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
                $a = New-ScheduledTaskAction -Execute $ps -Argument ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}" -Mode user -Job "{1}"' -f (Join-Path $udir 'savefiles.ps1'), $jf)
                if ($account) {
                    $t = New-ScheduledTaskTrigger -AtLogOn -User $account
                    $pr = New-ScheduledTaskPrincipal -UserId $account -LogonType Interactive -RunLevel Limited
                } else {
                    $t = New-ScheduledTaskTrigger -AtLogOn
                    $pr = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Limited
                }
                Register-ScheduledTask -TaskName 'AurOS put-back (your files)' -Action $a -Trigger $t -Principal $pr -Force `
                    -Description 'Saves the files you kept in AurOS in your Windows folders.' | Out-Null
                Log "the task that saves her files is registered for $account"
            }
            $until = (Get-Date).AddHours(12)
            while (-not (All-Saved $m.Rows $prof)) {
                if ($Once -or (Get-Date) -gt $until) { Log 'her files are not all saved yet'; exit 0 }
                Start-Sleep -Seconds 20
            }
        }
        Log 'every file is in her profile and was checked there'
        if (-not $Once -and -not $TestKeepTask) {
            Unregister-ScheduledTask -TaskName 'AurOS put-back (your files)' -Confirm:$false -ErrorAction SilentlyContinue
        }
        Remove-Partition -DiskNumber $disk.Number -PartitionNumber $hp.PartitionNumber -Confirm:$false
        Log 'the handover partition is removed'
    }

    # ---- the room, and hibernation, as they were ----------------------------------------
    $w = [uint64]$cfg.orig
    $s = [uint64]$cfg.shrunk
    $win = Get-Partition -DiskNumber $disk.Number | Where-Object { $_.Offset -eq [uint64]$cfg.win_offset } | Select-Object -First 1
    function Finish {
        if ($cfg.hib_file_on -eq 1) { & powercfg.exe /hibernate on }
        if ($cfg.hiberboot -ge 0) {
            Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -Value ([int]$cfg.hiberboot)
        }
        Status 'finished' 'Windows is back to the way it was.'
        if (-not $TestKeepTask) {
            Unregister-ScheduledTask -TaskName $cfg.task -Confirm:$false
            Remove-Item -Path $Params, $PSCommandPath -Force -ErrorAction SilentlyContinue
        }
    }
    if ($win.Size -ge $w) { Finish; exit 0 }
    if ($win.Size -lt $s) { exit 0 }
    $m2 = (Get-PartitionSupportedSize -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).SizeMax
    $t2 = [Math]::Min($w, $m2)
    if ($t2 -gt $win.Size) { Resize-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber -Size $t2 }
    if ((Get-Partition -DiskNumber $disk.Number -PartitionNumber $win.PartitionNumber).Size -ge $w) { Finish }
} catch {
    Log ('stopped: ' + $_.Exception.Message)
    Status 'error' 'AurOS could not finish putting your files in Windows. They are safe in the partition named AurOS-Handover, and this will try again the next time Windows starts.'
    exit 1
}
