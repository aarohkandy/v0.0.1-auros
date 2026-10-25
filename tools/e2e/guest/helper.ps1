# The test's eyes inside Windows. Served by the host (tools/e2e/vm.py) and
# run by C:\e2e\boot.ps1 at every logon, as the logged-in user.
#
# It long-polls the host for a question and posts the answer:
#   windows    every top-level window: title, class, process, rectangle
#   elements   buttons, links, check boxes, menu items... in the windows
#              whose title, class or process matches, with their names,
#              rectangles, tick state, and whether they are part of a web
#              page (UI Automation)
#   run        a read-only diagnostic (file hashes, folder listings)
#   diagnostic a read-only command in a child killed at its stated bound
#
# It never clicks, types or invokes anything: every press in this test is
# made by the host through QEMU's keyboard and tablet, after the host has
# taken a screenshot. UI Automation says where Microsoft's own buttons
# are, and the AurOS installer's (its screen-reader object, MSAA). The
# installer runs elevated, so this runs elevated too (a logon task with
# the highest run level, guest/firstlogon.ps1): a normal process cannot
# read an elevated window's controls.
param(
  [int]$MsaaProbePid = 0,
  [int64]$MsaaProbeHwnd = 0,
  [int]$MsaaProbeWaitSec = 0,
  [string]$MsaaProbePrimaryName = '',
  [int]$MsaaProbePrimaryEnabled = 1,
  [string]$MsaaProbeReadyFile = '',
  [switch]$BoundedDiagnosticSelftest,
  [string]$AccessibilityRequestBase64 = '',
  [int]$AccessibilityChildBlockMs = 0,
  [switch]$AccessibilityIsolationSelftest
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'    # Invoke-WebRequest's bar costs seconds
$hub = 'http://10.0.2.2:8000'

function Read-DiagnosticOutput([string]$path, [int]$maxBytes = 65536) {
  $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                            [IO.FileShare]::ReadWrite)
  try {
    $bytes = New-Object byte[] ($maxBytes + 1)
    $count = 0
    while ($count -lt $bytes.Length) {
      $got = $stream.Read($bytes, $count, $bytes.Length - $count)
      if ($got -le 0) { break }
      $count += $got
    }
    return @{
      text = [Text.Encoding]::UTF8.GetString($bytes, 0, [Math]::Min($count, $maxBytes)).TrimStart([char]0xfeff)
      truncated = $count -gt $maxBytes
    }
  } finally {
    $stream.Dispose()
  }
}

function Invoke-BoundedDiagnostic([string]$script, [int]$timeoutMs) {
  if ($timeoutMs -lt 1000 -or $timeoutMs -gt 45000) {
    throw 'diagnostic timeout must be from 1000 through 45000 ms'
  }
  $stdout = [IO.Path]::GetTempFileName()
  $stderr = [IO.Path]::GetTempFileName()
  $process = $null
  $verifiedExited = $false
  try {
    $preamble = '[Console]::OutputEncoding = [Text.Encoding]::UTF8' + "`n"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($preamble + $script))
    $exe = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $process = Start-Process -FilePath $exe -ArgumentList @(
      '-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded
    ) -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $status = 'ok'
    if (-not $process.WaitForExit($timeoutMs)) {
      try {
        Stop-Process -Id $process.Id -Force -ErrorAction Stop
      } catch {
        $status = 'termination-failed'
      }
      if ($process.WaitForExit(5000) -and $process.HasExited) {
        $verifiedExited = $true
        if ($status -ne 'termination-failed') { $status = 'timeout' }
      } else {
        $status = 'termination-failed'
      }
    } else {
      $verifiedExited = $true
      if ($process.ExitCode -ne 0) { $status = 'exit' }
    }
    $partial = ''
    $truncated = $false
    $outputRead = $true
    try {
      $read = Read-DiagnosticOutput $stdout
      $partial = $read.text
      $truncated = [bool]$read.truncated
    } catch { $outputRead = $false }
    return @{
      status = $status
      output = $partial
      output_read = $outputRead
      output_truncated = $truncated
      exit_code = $(if ($verifiedExited) { [int]$process.ExitCode } else { $null })
    }
  } finally {
    if ($process -and -not $verifiedExited) {
      try {
        Stop-Process -Id $process.Id -Force -ErrorAction Stop
        $verifiedExited = $process.WaitForExit(1000) -and $process.HasExited
      } catch { $verifiedExited = $false }
    }
    if (-not $process -or $verifiedExited) {
      Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
    }
  }
}

if ($BoundedDiagnosticSelftest) {
  $success = Invoke-BoundedDiagnostic "Write-Output 'bounded-success'" 5000
  if ($success.status -cne 'ok' -or -not $success.output_read -or
      $success.output.Trim() -cne 'bounded-success') {
    throw "bounded diagnostic success probe failed: $($success.status) '$($success.output)'"
  }
  $timeout = Invoke-BoundedDiagnostic "Write-Output 'partial-before-timeout'; [Console]::Out.Flush(); Start-Sleep 30" 5000
  if ($timeout.status -cne 'timeout' -or -not $timeout.output_read -or
      $timeout.output_truncated -or $timeout.output -notmatch 'partial-before-timeout') {
    throw "bounded diagnostic timeout probe failed: $($timeout.status) '$($timeout.output)'"
  }
  $large = Invoke-BoundedDiagnostic "[Console]::Out.Write(('x' * 70000)); [Console]::Out.Flush()" 10000
  if ($large.status -cne 'ok' -or -not $large.output_read -or
      -not $large.output_truncated -or $large.output.Length -ne 65536) {
    throw "bounded diagnostic truncation probe failed: $($large.status), truncated=$($large.output_truncated), length=$($large.output.Length)"
  }
  Write-Output 'bounded diagnostic native probe: PASS'
  exit 0
}

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -AssemblyName Accessibility
$accessibilityAssembly = [Accessibility.IAccessible].Assembly.Location
Add-Type -ReferencedAssemblies $accessibilityAssembly -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public sealed class E2eMsaaItem {
  public int Child;
  public int Role;
  public string Name;
  public int State;
  public int X;
  public int Y;
  public int Width;
  public int Height;
  public string KeyboardShortcut;
  public string DefaultAction;
}
public sealed class E2eMsaaSnapshotException : Exception {
  public readonly int ComResult;
  public E2eMsaaSnapshotException(string message, int comResult, Exception inner)
    : base(message, inner) { ComResult = comResult; }
}
public static class E2eUser32 {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder name, int count);
  [DllImport("user32.dll")]
  public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
  [DllImport("oleacc.dll")]
  private static extern int AccessibleObjectFromWindow(
    IntPtr hwnd, int objectId, ref Guid iid,
    [MarshalAs(UnmanagedType.Interface)] out Accessibility.IAccessible accessible);

  // Keep the RCW inside this typed bridge. Windows PowerShell cannot bind its
  // System.__ComObject back to IAccessible, and AurBridge intentionally does
  // not implement IDispatch name lookup.
  public static E2eMsaaItem[] ReadAccessible(IntPtr hwnd) {
    Guid iid = new Guid("618736e0-3c3d-11cf-810c-00aa00389b71");
    Accessibility.IAccessible accessible = null;
    int hr = AccessibleObjectFromWindow(hwnd, -4, ref iid, out accessible);
    if (hr != 0 || accessible == null) {
      throw new COMException(
        String.Format("AurBridge MSAA: AccessibleObjectFromWindow failed (HRESULT 0x{0:X8})", hr), hr);
    }
    try {
      int count = accessible.accChildCount;
      if (count < 0 || count > 80) {
        throw new InvalidOperationException("AurBridge MSAA: implausible child count " + count);
      }
      List<E2eMsaaItem> result = new List<E2eMsaaItem>(count + 1);
      for (int child = 0; child <= count; child++) {
        string operation = "accLocation";
        try {
          int x, y, width, height;
          accessible.accLocation(out x, out y, out width, out height, child);
          operation = "get_accRole";
          int role = Convert.ToInt32(accessible.get_accRole(child));
          operation = "get_accName";
          string name = accessible.get_accName(child);
          operation = "get_accState";
          int state = Convert.ToInt32(accessible.get_accState(child));
          operation = "get_accKeyboardShortcut";
          string keyboardShortcut = accessible.get_accKeyboardShortcut(child);
          operation = "get_accDefaultAction";
          string defaultAction = accessible.get_accDefaultAction(child);
          result.Add(new E2eMsaaItem {
            Child = child,
            Role = role,
            Name = name,
            State = state,
            X = x,
            Y = y,
            Width = width,
            Height = height,
            KeyboardShortcut = keyboardShortcut,
            DefaultAction = defaultAction
          });
        } catch (Exception ex) {
          int childHr = Marshal.GetHRForException(ex);
          throw new E2eMsaaSnapshotException(
            String.Format(
              "AurBridge MSAA child {0} {1} failed ({2}, HRESULT 0x{3:X8}): {4}",
              child, operation, ex.GetType().FullName, childHr, ex.Message), childHr, ex);
        }
      }
      return result.ToArray();
    } finally {
      if (Marshal.IsComObject(accessible)) {
        Marshal.FinalReleaseComObject(accessible);
      }
    }
  }
}
'@
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]

function Post($path, $obj) {
  $json = $obj | ConvertTo-Json -Depth 6 -Compress
  Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$hub$path" -TimeoutSec 30 `
    -Body ([Text.Encoding]::UTF8.GetBytes($json)) -ContentType 'application/json' | Out-Null
}

function Rect($e) {
  $r = $e.Current.BoundingRectangle
  if ($r.IsEmpty -or [double]::IsInfinity($r.Width)) { return $null }
  return @([int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height)
}

function ProcName($id) {
  try { (Get-Process -Id $id -ErrorAction Stop).ProcessName } catch { '' }
}

function IsTransientMsaaSnapshotError($record) {
  $exceptionCursor = $record.Exception
  while ($exceptionCursor) {
    if ($exceptionCursor.GetType().FullName -ceq 'E2eMsaaSnapshotException') {
      # E_INVALIDARG: the child stopped existing between child-count and its
      # getter. The disconnected HRESULTs are the equivalent window teardown.
      return $exceptionCursor.ComResult -in @(-2147024809, -2147417848, -2147220995)
    }
    $exceptionCursor = $exceptionCursor.InnerException
  }
  return $false
}

# AurBridge is one owner-drawn HWND. It deliberately exposes the widgets that
# receive its mouse and keyboard actions as simple children of an MSAA
# IAccessible object. The generic UI Automation Window proxy claims a top-level
# HWND before UIA's later MSAA proxy gets a chance, so FindAll(Descendants)
# sees the window but none of those real controls. Read that exact OBJID_CLIENT
# object when, and only when, this is the exact window already selected above.
function AurBridgeMsaaElements($w, $types, $max) {
  $c = $w.Current
  $hwnd = [IntPtr][int64]$c.NativeWindowHandle
  if ($hwnd -eq [IntPtr]::Zero) { throw 'AurBridge MSAA: the selected window has no HWND' }

  [uint32]$windowPid = 0
  if (-not [E2eUser32]::GetWindowThreadProcessId($hwnd, [ref]$windowPid) -or
      $windowPid -ne [uint32]$c.ProcessId) {
    throw 'AurBridge MSAA: the selected HWND is stale or belongs to another process'
  }
  $class = New-Object Text.StringBuilder 80
  if (-not [E2eUser32]::GetClassName($hwnd, $class, $class.Capacity) -or
      $class.ToString() -cne 'AurBridgeWizard' -or $c.ClassName -cne 'AurBridgeWizard') {
    throw 'AurBridge MSAA: the selected HWND is not the exact AurBridgeWizard window'
  }

  $roleNames = @{ 0x0a = 'Pane'; 0x2b = 'Button'; 0x2c = 'CheckBox'; 0x2d = 'RadioButton' }
  $wanted = @{}; foreach ($t in $types) { $wanted[[string]$t] = $true }
  $out = @()
  # CHILDID_SELF (0) is the warning Pane; positive IDs are the exact simple
  # children backed by the wizard's current widget list. The typed bridge owns
  # and releases the COM object before returning these plain managed records.
  foreach ($item in @([E2eUser32]::ReadAccessible($hwnd))) {
      $child = [int]$item.Child
      try {
        $role = [int]$item.Role
        $type = $roleNames[$role]
        if (-not $type) {
          throw ('unexpected role 0x{0:x}' -f $role)
        }
        if (-not $wanted.ContainsKey($type)) { continue }
        $name = [string]$item.Name
        $state = [int]$item.State
        [int]$x = $item.X; [int]$y = $item.Y
        [int]$width = $item.Width; [int]$height = $item.Height
        if (-not $name -or $width -le 0 -or $height -le 0) {
          throw 'empty name or rectangle'
        }
        # Invisible/off-screen MSAA children are not current controls. A
        # disabled one remains visible in the result with enabled=false so
        # the caller's exact security predicate rejects it explicitly.
        if (($state -band 0x8000) -or ($state -band 0x10000)) { continue }
        $key = [string]$item.KeyboardShortcut
        $action = [string]$item.DefaultAction
        if ($child -gt 0 -and -not $action) { throw 'control has no default action' }
        $toggle = $null
        if ($type -ceq 'CheckBox') {
          $toggle = if ($state -band 0x10) { 'On' } else { 'Off' }
        }
        $out += [pscustomobject]@{
          name = $name; type = $type; rect = @($x, $y, $width, $height)
          enabled = -not [bool]($state -band 0x1); state = $state
          toggle = $toggle; doc = 0
          window = $c.Name; proc = (ProcName $windowPid); pid = $windowPid; aid = "msaa:$child"
          key = $key; action = $action; hwnd = $hwnd.ToInt64(); child = $child
          source = 'AurBridge MSAA OBJID_CLIENT'
        }
        if ($out.Count -ge $max) { break }
      } catch {
        throw "AurBridge MSAA child $child could not be read exactly: $_"
      }
  }
  return $out
}

function TopWindows {
  $fg = [int64][E2eUser32]::GetForegroundWindow()
  $out = @()
  foreach ($w in $AE::RootElement.FindAll($TS::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
    try {
      $c = $w.Current
      $out += [pscustomobject]@{
        name = $c.Name; cls = $c.ClassName; proc = (ProcName $c.ProcessId)
        pid = $c.ProcessId; rect = (Rect $w); hwnd = [int64]$c.NativeWindowHandle
        foreground = ([int64]$c.NativeWindowHandle -eq $fg); offscreen = $c.IsOffscreen
      }
    } catch { }
  }
  return $out
}

function Elements($match, $types, $max) {
  $conds = @()
  foreach ($t in $types) {
    $ct = [System.Windows.Automation.ControlType]::$t
    if ($ct) { $conds += New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $ct) }
  }
  if ($conds.Count -eq 0) { return @() }
  if ($conds.Count -eq 1) { $cond = $conds[0] }
  else { $cond = New-Object System.Windows.Automation.OrCondition(,[System.Windows.Automation.Condition[]]$conds) }
  $out = @()
  foreach ($w in $AE::RootElement.FindAll($TS::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
    $isAurBridge = $false
    try {
      $c = $w.Current
      $p = ProcName $c.ProcessId
      if ($match -and -not ($c.Name -match $match -or $c.ClassName -match $match -or $p -match $match)) { continue }
      if ($c.ClassName -ceq 'AurBridgeWizard') {
        $isAurBridge = $true
        $out += @(AurBridgeMsaaElements $w $types ($max - $out.Count))
        if ($out.Count -ge $max) { return $out }
        continue
      }
      # Which of them belong to a web document, and how big it is: a
      # website fills the window, while Edge's own flyouts (downloads,
      # menus) are small documents too. The host uses the size so that a
      # page's own words ("press Keep anyway") are never taken for the
      # browser's buttons.
      $docIds = @{}
      try {
        $dcond = New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, [System.Windows.Automation.ControlType]::Document)
        foreach ($d in $w.FindAll($TS::Descendants, $dcond)) {
          $dr = Rect $d
          $area = 1
          if ($dr) { $area = [int64]$dr[2] * [int64]$dr[3] }
          foreach ($x in $d.FindAll($TS::Descendants, $cond)) {
            $k = ($x.GetRuntimeId() -join '.')
            if (-not $docIds.ContainsKey($k) -or $docIds[$k] -lt $area) { $docIds[$k] = $area }
          }
        }
      } catch { }
      foreach ($e in $w.FindAll($TS::Descendants, $cond)) {
        try {
          $ec = $e.Current
          if ($ec.IsOffscreen) { continue }
          $r = Rect $e
          if (-not $r -or $r[2] -le 0 -or $r[3] -le 0) { continue }
          $tog = $null
          try { $tog = "$($e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState)" } catch { }
          $out += [pscustomobject]@{
            name = $ec.Name; type = $ec.ControlType.ProgrammaticName -replace '^ControlType\.', ''
            rect = $r; enabled = $ec.IsEnabled; toggle = $tog
            doc = [int64]$docIds[($e.GetRuntimeId() -join '.')]
            window = $c.Name; proc = $p; aid = $ec.AutomationId
            # the key that presses it (the AurOS installer marks its primary
            # button "Enter" in its screen-reader object)
            key = "$($ec.AccessKey)$($ec.AcceleratorKey)"
          }
          if ($out.Count -ge $max) { return $out }
        } catch { }
      }
    } catch {
      # Ordinary Windows UIA trees race with windows opening and closing, so
      # their historic best-effort behavior remains. AurBridge is the exact
      # security-sensitive window selected above: an unreadable MSAA tree is
      # an explicit helper error, never an empty result that invites OCR.
      if ($isAurBridge) { throw }
    }
  }
  return $out
}

function AccessibilityChildResult([hashtable]$request) {
  $allowed = @('op', 'match', 'types', 'max')
  foreach ($name in @($request.Keys)) {
    if ($allowed -cnotcontains [string]$name) { throw "unknown accessibility request field '$name'" }
  }
  $op = [string]$request.op
  if ($op -ceq 'windows') {
    if ($request.Keys.Count -ne 1) { throw 'windows accessibility request has extra fields' }
    return @{ ok = $true; status = 'ok'; windows = @(TopWindows) }
  }
  if ($op -cne 'elements') { throw "unknown accessibility operation '$op'" }
  if (-not $request.ContainsKey('types') -or $null -eq $request.types) {
    throw 'elements accessibility request requires types'
  }
  $types = @($request.types)
  if ($types.Count -lt 1 -or $types.Count -gt 16) { throw 'elements types count is outside 1..16' }
  foreach ($type in $types) {
    if ($type -isnot [string] -or $type.Length -lt 1 -or $type.Length -gt 40 -or
        $type -cnotmatch '^[A-Za-z]+$') { throw 'elements contains an invalid control type' }
  }
  $match = if ($request.ContainsKey('match')) { [string]$request.match } else { '' }
  if ($match.Length -gt 512) { throw 'elements match is longer than 512 characters' }
  $max = if ($request.ContainsKey('max')) { [int]$request.max } else { 400 }
  if ($max -lt 1 -or $max -gt 400) { throw 'elements max is outside 1..400' }
  return @{ ok = $true; status = 'ok'; elements = @(Elements $match $types $max) }
}

function Parse-AccessibilityChildOutput($diagnostic) {
  if ($diagnostic.status -cne 'ok') { throw "child-$($diagnostic.status)" }
  if (-not $diagnostic.output_read) { throw 'child-output-unreadable' }
  if ($diagnostic.output_truncated) { throw 'child-output-truncated' }
  $text = [string]$diagnostic.output
  $trimmed = $text.Trim()
  if (-not $trimmed -or $trimmed[0] -cne '{' -or $trimmed[$trimmed.Length - 1] -cne '}' -or
      $trimmed.Contains("`n") -or $trimmed.Contains("`r")) {
    throw 'child-output-not-one-json-record'
  }
  try { $result = $trimmed | ConvertFrom-Json -ErrorAction Stop }
  catch { throw 'child-output-invalid-json' }
  if ($null -eq $result -or $result -isnot [pscustomobject] -or
      $result.PSObject.Properties.Name -cnotcontains 'ok' -or
      $result.PSObject.Properties.Name -cnotcontains 'status') {
    throw 'child-output-invalid-schema'
  }
  return $result
}

function Invoke-IsolatedAccessibility([hashtable]$request, [int]$timeoutMs = 20000,
                                      [int]$testBlockMs = 0) {
  if (-not $PSCommandPath -or -not (Test-Path -LiteralPath $PSCommandPath -PathType Leaf)) {
    return [pscustomobject]@{ ok = $false; status = 'helper-path-unavailable'; error = 'accessibility helper path is unavailable' }
  }
  $requestJson = $request | ConvertTo-Json -Depth 6 -Compress
  $requestBytes = [Text.Encoding]::UTF8.GetBytes($requestJson)
  if ($requestBytes.Length -gt 8192) {
    return [pscustomobject]@{ ok = $false; status = 'request-too-large'; error = 'accessibility request exceeds 8192 bytes' }
  }
  $request64 = [Convert]::ToBase64String($requestBytes)
  $quotedPath = $PSCommandPath.Replace("'", "''")
  $script = "& '$quotedPath' -AccessibilityRequestBase64 '$request64' -AccessibilityChildBlockMs $testBlockMs"
  $diagnostic = Invoke-BoundedDiagnostic $script $timeoutMs
  try { return Parse-AccessibilityChildOutput $diagnostic }
  catch {
    $reason = [string]$_
    return [pscustomobject]@{
      ok = $false; status = $reason; error = "isolated accessibility request failed: $reason"
      test_ready = [bool]($testBlockMs -gt 0 -and $diagnostic.output_read -and
        -not $diagnostic.output_truncated -and $diagnostic.output -ceq 'accessibility-child-ready')
    }
  }
}

if ($AccessibilityRequestBase64) {
  $child = $null
  try {
    if ($AccessibilityRequestBase64.Length -gt 12000 -or
        $AccessibilityRequestBase64 -cnotmatch '^[A-Za-z0-9+/]+={0,2}$') {
      throw 'invalid accessibility request encoding'
    }
    $bytes = [Convert]::FromBase64String($AccessibilityRequestBase64)
    if ($bytes.Length -gt 8192) { throw 'accessibility request exceeds 8192 bytes' }
    $json = [Text.UTF8Encoding]::new($false, $true)
    $requestObject = ($json.GetString($bytes) | ConvertFrom-Json -ErrorAction Stop)
    if ($requestObject -isnot [pscustomobject]) { throw 'accessibility request must be one object' }
    $request = @{}
    foreach ($property in $requestObject.PSObject.Properties) { $request[$property.Name] = $property.Value }
    if ($AccessibilityChildBlockMs -lt 0 -or $AccessibilityChildBlockMs -gt 30000) {
      throw 'accessibility child block is outside 0..30000 ms'
    }
    if ($AccessibilityChildBlockMs) {
      [Console]::Out.Write('accessibility-child-ready')
      [Console]::Out.Flush()
      Start-Sleep -Milliseconds $AccessibilityChildBlockMs
    }
    $child = AccessibilityChildResult $request
  } catch {
    $child = @{ ok = $false; status = 'request-error'; error = [string]$_ }
  }
  $output = $child | ConvertTo-Json -Depth 8 -Compress
  if ([Text.Encoding]::UTF8.GetByteCount($output) -gt 60000) {
    $output = @{ ok = $false; status = 'response-too-large'; error = 'accessibility response exceeds 60000 bytes' } |
      ConvertTo-Json -Compress
  }
  [Console]::Out.WriteLine($output)
  exit 0
}

if ($AccessibilityIsolationSelftest) {
  if ($MsaaProbePid -le 0 -or $MsaaProbeHwnd -le 0) {
    throw 'accessibility isolation selftest requires an exact PID and HWND'
  }
  $fake = @{
    status = 'ok'; output_read = $true; output_truncated = $false
    output = '{"ok":true,"status":"ok"}' + [Environment]::NewLine + '{"extra":true}'
  }
  try { [void](Parse-AccessibilityChildOutput $fake); throw 'extra JSON output was accepted' }
  catch { if ([string]$_ -cnotmatch 'child-output-not-one-json-record') { throw } }
  $fake.output = '{"ok":true,"status":"ok"}'; $fake.output_truncated = $true
  try { [void](Parse-AccessibilityChildOutput $fake); throw 'truncated JSON output was accepted' }
  catch { if ([string]$_ -cnotmatch 'child-output-truncated') { throw } }

  $request = @{ op = 'elements'; match = '^AurBridgeWizard$'; types = @('Button','RadioButton','CheckBox','Pane'); max = 80 }
  $timed = Invoke-IsolatedAccessibility $request 5000 30000
  if ($timed.ok -or $timed.status -cnotmatch 'child-timeout' -or -not $timed.test_ready) {
    throw "blocked accessibility child did not time out exactly: $($timed.status)"
  }
  $recovered = Invoke-IsolatedAccessibility $request 20000
  if (-not $recovered.ok -or $recovered.status -cne 'ok') {
    throw "accessibility did not recover after timeout: $($recovered.status)"
  }
  $exact = @($recovered.elements | Where-Object {
    $_.pid -eq $MsaaProbePid -and [int64]$_.hwnd -eq $MsaaProbeHwnd -and
    $_.source -ceq 'AurBridge MSAA OBJID_CLIENT'
  })
  if ($exact.Count -lt 2 -or @($recovered.elements | Where-Object {
      $_.source -ceq 'AurBridge MSAA OBJID_CLIENT' -and
      ($_.pid -ne $MsaaProbePid -or [int64]$_.hwnd -ne $MsaaProbeHwnd)
    }).Count) {
    throw 'recovered accessibility result did not retain exact PID/HWND authority'
  }
  Write-Output 'accessibility isolation timeout and recovery: PASS'
  exit 0
}

# The Windows source test invokes this exact production helper from another
# process. It proves the cross-process boundary that the wizard's own
# in-process accessibility selftest cannot exercise.
if ($MsaaProbePid -or $MsaaProbeHwnd -or $MsaaProbeWaitSec) {
  if ($MsaaProbeReadyFile) {
    [IO.File]::WriteAllText($MsaaProbeReadyFile, "$PID", [Text.Encoding]::ASCII)
  }
  $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(1, $MsaaProbeWaitSec))
  $lastProbe = 'exact_windows=0'
  $lastReadError = ''
  $stableSignature = $null
  do {
    $wins = @($AE::RootElement.FindAll(
      $TS::Children, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object {
        try {
          $wc = $_.Current
          $wc.ClassName -ceq 'AurBridgeWizard' -and
          (!$MsaaProbePid -or $wc.ProcessId -eq $MsaaProbePid) -and
          (!$MsaaProbeHwnd -or [int64]$wc.NativeWindowHandle -eq $MsaaProbeHwnd)
        } catch { $false }
      })
    $lastProbe = "exact_windows=$($wins.Count)"
    if ($wins.Count -ne 1) { $stableSignature = $null }
    if ($wins.Count -gt 1) {
      throw 'AurBridge MSAA probe: the requested process/HWND is ambiguous'
    }
    if ($wins.Count -eq 1) {
      try {
        $items = @(AurBridgeMsaaElements $wins[0] @('Button','RadioButton','CheckBox','Pane') 80)
      } catch {
        # --selftest advances its real window through pages and exits quickly.
        # A page change can invalidate a captured child count mid-enumeration.
        # Only this bounded native probe retries the whole exact snapshot;
        # production element reads return the explicit error to their caller.
        if (-not $MsaaProbeWaitSec -or -not (IsTransientMsaaSnapshotError $_)) { throw }
        $stableSignature = $null
        $lastReadError = "read_error=$_"
        Start-Sleep -Milliseconds 50
        continue
      }
      $primary = @($items | Where-Object {
        $_.type -ceq 'Button' -and $_.key -ceq 'Enter' -and
        (!$MsaaProbePrimaryName -or $_.name -ceq $MsaaProbePrimaryName) -and
        $_.enabled -eq [bool]$MsaaProbePrimaryEnabled
      })
      $buttons = @($items | Where-Object { $_.type -ceq 'Button' })
      $enters = @($buttons | Where-Object { $_.key -ceq 'Enter' })
      $lastProbe = "exact_windows=1 pid=$($items[0].pid) controls=$($items.Count) buttons=$($buttons.Count) enter=$($enters.Count) expected=$($primary.Count)"
      if (-not $MsaaProbeWaitSec -or $primary.Count -eq 1) {
        if ($MsaaProbeWaitSec) {
          # Two identical complete reads prove this was a stable page, not a
          # mixture of the widget lists on either side of a render transition.
          $signature = $items | ConvertTo-Json -Depth 6 -Compress
          if ($signature -cne $stableSignature) {
            $stableSignature = $signature
            Start-Sleep -Milliseconds 50
            continue
          }
        }
        $items | ConvertTo-Json -Depth 6 -Compress
        exit 0
      }
      $stableSignature = $null
    }
    Start-Sleep -Milliseconds 50
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "AurBridge MSAA probe: no single expected Enter button appeared on the exact process/HWND ($MsaaProbePrimaryName, enabled=$([bool]$MsaaProbePrimaryEnabled)); $lastProbe; $lastReadError"
}

$who = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
Post '/e2e/event?name=helper-up' @{
  user = $env:USERNAME; pid = $PID
  elevated = $who.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

while ($true) {
  try {
    $r = Invoke-WebRequest -UseBasicParsing "$hub/e2e/cmd" -TimeoutSec 40
  } catch { Start-Sleep 2; continue }
  if ($r.StatusCode -ne 200) { continue }
  $text = $r.Content
  if ($text -is [byte[]]) { $text = [Text.Encoding]::UTF8.GetString($text) }
  if (-not $text) { continue }
  $c = $text | ConvertFrom-Json
  $res = @{ id = $c.id; ok = $true }
  $t0 = Get-Date
  try {
    switch ($c.op) {
      'windows'  {
        $a = Invoke-IsolatedAccessibility @{ op = 'windows' }
        $res.accessibility_status = $a.status
        if (-not $a.ok) { $res.ok = $false; $res.error = $a.error }
        else { $res.windows = @($a.windows) }
      }
      'elements' {
        $max = 400; if ($c.max) { $max = [int]$c.max }
        $a = Invoke-IsolatedAccessibility @{
          op = 'elements'; match = [string]$c.match; types = @($c.types); max = $max
        }
        $res.accessibility_status = $a.status
        if (-not $a.ok) { $res.ok = $false; $res.error = $a.error }
        else { $res.elements = @($a.elements) }
      }
      'run'      { $res.output = (Invoke-Expression $c.script 2>&1 | Out-String -Width 220) }
      'diagnostic' {
        $d = Invoke-BoundedDiagnostic $c.script ([int]$c.timeout_ms)
        $res.output = $d.output
        $res.diagnostic_status = $d.status
        $res.diagnostic_exit_code = $d.exit_code
        $res.diagnostic_output_read = $d.output_read
        $res.diagnostic_output_truncated = $d.output_truncated
      }
      default    { $res.ok = $false; $res.error = "unknown op $($c.op)" }
    }
  } catch { $res.ok = $false; $res.error = "$_" }
  $res.ms = [int]((Get-Date) - $t0).TotalMilliseconds
  try { Post "/e2e/result?id=$($c.id)" $res } catch { }
}
