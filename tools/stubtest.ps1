# stubtest.ps1 -- the program she downloads takes nothing from the folder it sits in (SEC-003).
#
#   pwsh tools/stubtest.ps1 -Exe exe -Stubs stubtest -Canary canary
#
# On a real Windows with administrator rights (windows.yml, windows-source). exe/ holds
# aurbridge-setup.exe (the downloaded program: kernel32 only, it runs the installer from
# a private folder) and aurbridge-wizard.exe (that installer); stubtest/ the test builds
# of the stub; canary/ canary.dll (writes "<name>.loaded" when anything loads it).
#
# Every check below that says "must" has a twin that says "the mutant does": a version
# of the stub made wrong in the one way the check is about, which the check must catch.
# A check whose twin passes proves nothing and fails the run.
param([string]$Exe = 'exe', [string]$Stubs = 'stubtest', [string]$Canary = 'canary', [switch]$IsolatedRunner)
$ErrorActionPreference = 'Stop'
if (-not $IsolatedRunner -or $env:GITHUB_ACTIONS -ne 'true') {
    Write-Host '::error::stubtest stops setup.exe processes and cleans its ProgramData test folders; run it only on an isolated GitHub runner with -IsolatedRunner'
    exit 1
}
$root = Join-Path $env:RUNNER_TEMP 'stubtest'
Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $root | Out-Null
function Die($m) { Write-Host "::error::$m"; exit 1 }
function Ok($m) { "ok: $m" }
$base = [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonApplicationData)
if (-not $base) { Die 'Windows did not report its common application-data folder' }
$fixed = Join-Path $base 'AurOS-Setup-0123456789abcdef'     # the name the test builds use

# every name a planted DLL could be taken under: what the installer loads by name, what
# Windows' own DLLs ask for (run 40047918166), and the usual hijack names
$names = 'winhttp.dll','oleacc.dll','wlanapi.dll','iphlpapi.dll','winspool.drv','wtsapi32.dll','dwmapi.dll',
         'cfgmgr32.dll','cryptbase.dll','cryptsp.dll','comctl32.dll','uxtheme.dll','version.dll','sechost.dll','bcrypt.dll'

# a folder like Downloads: the program, and a planted copy of every name beside it
function New-Downloads($name, $exeFile) {
    $d = Join-Path $root $name
    New-Item -ItemType Directory -Force -Path "$d\r" | Out-Null
    Copy-Item $exeFile "$d\AurOS-Setup.exe"
    foreach ($n in $names) { Copy-Item "$Canary\canary.dll" (Join-Path $d $n) }
    return $d
}
function Planted($d) { @(Get-ChildItem $d -Filter *.loaded -ErrorAction SilentlyContinue | ForEach-Object { $_.Name -replace '\.loaded$','' }) }
# start, wait at most $secs, and say how it ended ('HUNG' when it had to be killed)
function Run($file, $arguments, $cwd, $secs = 40) {
    $p = Start-Process -FilePath $file -ArgumentList $arguments -WorkingDirectory $cwd -PassThru
    if (-not $p.WaitForExit($secs * 1000)) {
        Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        $p.Kill(); return 'HUNG'
    }
    return $p.ExitCode
}
function Stray() { @(Get-ChildItem $base -Directory -Filter 'AurOS-Setup-*' -ErrorAction SilentlyContinue | Where-Object { -not ($_.Attributes -band 'ReparsePoint') }) }
Get-ChildItem $base -Directory -Filter 'AurOS-Setup-*' -ErrorAction SilentlyContinue | ForEach-Object { cmd /c rmdir "$($_.FullName)" 2>$null }

# ── 1. the hole is real here: the installer, started the old way, takes planted DLLs ──
$d = New-Downloads 'old-way' "$Exe\aurbridge-wizard.exe"
$code = Run "$d\AurOS-Setup.exe" "--report `"$d\r`"" $d 20
$took = @(Planted $d)
Ok "the installer started from Downloads, the old way: $code, took $($took -join ', ')"
if (-not $took) { Die "the planted DLLs were not taken by the installer started the old way, so this test cannot tell a safe program from an unsafe one on this runner (exit $code)" }

# ── 2. the downloaded program, in the same folder, takes none ──
$d = New-Downloads 'stub' "$Exe\aurbridge-setup.exe"
$codes = @()
foreach ($a in @("--quiet-failure --report `"$d\r`"", "--quiet-failure --navtest `"$d\r`"", "--quiet-failure --selftest `"$d\r`"")) {
    $codes += "$a -> $(Run "$d\AurOS-Setup.exe" $a $d)"
}
"stub runs: $($codes -join ' | ')"
$hit = @(Planted $d)
if ($hit) { Die "the downloaded program loaded a planted DLL: $($hit -join ', ')" }
foreach ($c in $codes) { if ($c -notmatch '-> 0$') { Die "the downloaded program did not run to its end: $c" } }
if (-not (Test-Path "$d\r\AurOS-report.txt")) { Die "the installer did not run (no report)" }
if (-not (Select-String -Path "$d\r\selftest.log" -Pattern 'screen reader    : "[^"]{10,}"' -Quiet)) { Die "the installer's window test did not reach the screen reader" }
if (Select-String -Path "$d\r\navtest.log" -Pattern '\[FAIL\]' -Quiet) { Die "the installer's own tests failed when started through the downloaded program" }
if (Stray) { Die "a private folder was left behind: $((Stray | ForEach-Object Name) -join ', ')" }
Ok "the downloaded program took none of $($names.Count) planted DLLs (the same files that stopped the installer started the old way), ran the installer through to its end, and left nothing behind"

# ── 3. the private folder: ours, and only ours, while the installer runs ──
function Inspect-Private($stubFile, $what) {
    $dd = New-Downloads "acl-$what" $stubFile
    Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath "$dd\AurOS-Setup.exe" -ArgumentList '--quiet-failure' -WorkingDirectory $dd -PassThru   # the window stays open
    $priv = $null
    for ($i = 0; $i -lt 150 -and -not $priv; $i++) { Start-Sleep -Milliseconds 100; $priv = Stray | Select-Object -First 1 }
    if (-not $priv) { $p.Kill(); return @{ problem = 'no private folder appeared'; why = '' } }
    Start-Sleep -Seconds 2
    $acl = Get-Acl $priv.FullName
    $problems = @()
    if ($acl.Owner -ne 'BUILTIN\Administrators') { $problems += "owner is $($acl.Owner)" }
    if (-not $acl.AreAccessRulesProtected) { $problems += 'it inherits its parent''s permissions' }
    $who = @($acl.Access | ForEach-Object { "$($_.IdentityReference):$($_.FileSystemRights):$($_.AccessControlType)" } | Sort-Object)
    $want = @('BUILTIN\Administrators:FullControl:Allow', 'NT AUTHORITY\SYSTEM:FullControl:Allow')
    if (($who -join ';') -ne ($want -join ';')) { $problems += "its permissions are $($who -join ' + ')" }
    $files = @(Get-ChildItem $priv.FullName -Force | ForEach-Object Name)
    if (($files -join ',') -ne 'setup.exe') { $problems += "it holds: $($files -join ', ')" }
    $proc = @(Get-CimInstance Win32_Process -Filter "Name='setup.exe'")
    if (-not ($proc | Where-Object { $_.ExecutablePath -eq (Join-Path $priv.FullName 'setup.exe') })) { $problems += 'the installer is not running from it' }
    Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $null = $p.WaitForExit(20000)
    if (-not $p.HasExited) { $p.Kill() }
    Start-Sleep -Milliseconds 500
    if (Test-Path $priv.FullName) { $problems += 'it was not removed afterwards' }
    return @{ problem = ($problems -join '; '); why = $priv.Name }
}
$r = Inspect-Private "$Exe\aurbridge-setup.exe" 'real'
if ($r.problem) { Die "the private folder is wrong: $($r.problem)" }
Ok "while the installer runs, its folder $($r.why) is owned by Administrators, protected, SYSTEM and Administrators only, holds setup.exe only, and goes when it ends"
$r2 = Inspect-Private "$Exe\aurbridge-setup.exe" 'real-again'
if ($r2.problem) { Die "the private folder is wrong on a second run: $($r2.problem)" }
if ($r2.why -eq $r.why) { Die "two runs used the same private folder name: $($r.why)" }
Ok "two runs used distinct Windows-generated random folder names ($($r.why), $($r2.why))"
$r = Inspect-Private "$Stubs\aurbridge-setup-inherit-mutant.exe" 'inherit'
if (-not $r.problem) { Die "the check of the private folder passed a stub that made it with inherited permissions: it proves nothing" }
Ok "the same check catches a stub that makes the folder with inherited permissions ($($r.problem))"
Get-ChildItem $base -Directory -Filter 'AurOS-Setup-*' -ErrorAction SilentlyContinue | ForEach-Object { icacls $_.FullName /grant 'BUILTIN\Administrators:(OI)(CI)F' /T /C 2>$null | Out-Null; Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }

# ── 4. a link where the folder should be is refused ──
function Plant-Junction($target) {
    Remove-Item $fixed -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    cmd /c mklink /J "$fixed" "$target" | Out-Null
    if (-not ((Get-Item $fixed -Force).Attributes -band 'ReparsePoint')) { Die 'the test could not make a junction' }
}
$target = Join-Path $root 'junction-target'
$dd = New-Downloads 'junction-real' "$Stubs\aurbridge-setup-test.exe"
Plant-Junction $target
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 30
$ran = Test-Path "$dd\r\AurOS-report.txt"
$left = @(Get-ChildItem $target -Force | ForEach-Object Name)
if ($code -eq 0 -or $ran -or $left) { Die "a junction at the folder's name was not refused: exit $code, the installer ran: $ran, the target holds: $($left -join ', ')" }
Ok "a junction planted at the folder's name is refused (exit $code): the installer did not run and nothing is written through it"
$dd = New-Downloads 'junction-mutant' "$Stubs\aurbridge-setup-accept-mutant.exe"
Plant-Junction $target
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 30
$ran = Test-Path "$dd\r\AurOS-report.txt"
if (-not ($ran -and $code -eq 0)) { Die "the junction check passed a stub that accepts a folder that is already there: it proves nothing (exit $code, the installer ran: $ran)" }
Ok "the same check catches a stub that accepts what is already there: it ran the installer from inside the junction's target"
cmd /c rmdir "$fixed" 2>$null; Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue

# a folder that is already there, and is not a link, is refused too
New-Item -ItemType Directory -Force -Path $fixed | Out-Null
$dd = New-Downloads 'exists' "$Stubs\aurbridge-setup-test.exe"
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 30
if ($code -eq 0 -or (Test-Path "$dd\r\AurOS-report.txt")) { Die "a folder that was already there was accepted: exit $code" }
Ok "a folder that is already there is refused, not shared (exit $code)"
Remove-Item $fixed -Recurse -Force -ErrorAction SilentlyContinue

# the bytes on disk are checked against the hash baked into the actual stub code
$dd = New-Downloads 'bad-hash-mutant' "$Stubs\aurbridge-setup-bad-hash-mutant.exe"
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 20
if ($code -ne 3 -or (Test-Path "$dd\r\AurOS-report.txt")) { Die "a stub with the wrong baked-in installer hash ran its installer: exit $code" }
if (Stray) { Die "the bad-hash refusal left a private folder behind: $((Stray | ForEach-Object Name) -join ', ')" }
Ok "the extracted installer is refused before launch when its SHA-256 does not match the hash baked into the stub"

# ── 5. beside Downloads is the one place the installer must not run from ──
$dd = New-Downloads 'here-mutant' "$Stubs\aurbridge-setup-here-mutant.exe"
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 20
$took = @(Planted $dd)
if (-not $took) { Die "the planted DLLs were not taken by a stub that runs the installer from Downloads: the check proves nothing (exit $code)" }
Ok "the same planted DLLs are caught when a stub runs the installer from Downloads (exit $code, took $($took -join ', '))"
Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# ── 6. what a run leaves behind: only ours, by name AND by permissions, is removed ──
Get-ChildItem $base -Directory -Filter 'AurOS-Setup-*' -ErrorAction SilentlyContinue | ForEach-Object { icacls $_.FullName /grant 'BUILTIN\Administrators:(OI)(CI)F' /T /C 2>$null | Out-Null; Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
$ours = Join-Path $base 'AurOS-Setup-aaaaaaaaaaaaaaaa'            # our name, but made the ordinary way: its permissions are not ours
$near = Join-Path $base 'AurOS-Setup-keep-this'                   # not our name
$other = Join-Path $base 'Other-aaaaaaaaaaaaaaaa'
foreach ($x in $ours, $near, $other) { New-Item -ItemType Directory -Force -Path $x | Out-Null; Set-Content "$x\setup.exe" 'not yours to delete' }
# and a real leftover: a run that was cut short (the program killed while the installer runs)
$dd = New-Downloads 'leftover' "$Exe\aurbridge-setup.exe"
Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$dd\AurOS-Setup.exe" -ArgumentList '--quiet-failure' -WorkingDirectory $dd -PassThru
$mine = $null
for ($i = 0; $i -lt 150 -and -not $mine; $i++) { Start-Sleep -Milliseconds 100; $mine = Stray | Where-Object { $_.FullName -ne $ours -and $_.Name -ne 'AurOS-Setup-keep-this' } | Select-Object -First 1 }
if (-not $mine) { Die 'no private folder appeared for the leftover test' }
$p.Kill(); Get-Process -Name setup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue   # the machine "restarted"
Start-Sleep -Seconds 1
if (-not (Test-Path $mine.FullName)) { Die 'the leftover was removed by something other than the next run' }
$code = Run "$dd\AurOS-Setup.exe" "--quiet-failure --report `"$dd\r`"" $dd 40
if (Test-Path $mine.FullName) { Die "the next run did not remove a leftover that is ours: $($mine.Name)" }
foreach ($x in $ours, $near, $other) { if (-not (Test-Path "$x\setup.exe")) { Die "the cleanup removed something that is not ours: $x" } }
Ok "the next run removed the leftover that is ours ($($mine.Name)) and nothing else: a folder with our name but not our permissions, one with a near name, and another program's folder all stand"
foreach ($x in $ours, $near, $other) { Remove-Item $x -Recurse -Force -ErrorAction SilentlyContinue }
Get-ChildItem $base -Directory -Filter 'AurOS-Setup-*' -ErrorAction SilentlyContinue | ForEach-Object { icacls $_.FullName /grant 'BUILTIN\Administrators:(OI)(CI)F' /T /C 2>$null | Out-Null; Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
"### The downloaded program (SEC-003)`nA planted copy of $($names.Count) DLL names beside it was never loaded; the installer started the old way took them. Its private folder was checked while the installer ran, and a junction, an existing folder, inherited permissions and Downloads itself were each refused or caught by a mutant." | Out-File $env:GITHUB_STEP_SUMMARY -Append
exit 0
