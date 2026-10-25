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
  [string]$MsaaProbeReadyFile = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'    # Invoke-WebRequest's bar costs seconds
$hub = 'http://10.0.2.2:8000'
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
        try {
          int x, y, width, height;
          accessible.accLocation(out x, out y, out width, out height, child);
          result.Add(new E2eMsaaItem {
            Child = child,
            Role = Convert.ToInt32(accessible.get_accRole(child)),
            Name = accessible.get_accName(child),
            State = Convert.ToInt32(accessible.get_accState(child)),
            X = x,
            Y = y,
            Width = width,
            Height = height,
            KeyboardShortcut = accessible.get_accKeyboardShortcut(child),
            DefaultAction = accessible.get_accDefaultAction(child)
          });
        } catch (Exception ex) {
          throw new InvalidOperationException(
            "AurBridge MSAA child " + child + " could not be read exactly", ex);
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

# The Windows source test invokes this exact production helper from another
# process. It proves the cross-process boundary that the wizard's own
# in-process accessibility selftest cannot exercise.
if ($MsaaProbePid -or $MsaaProbeHwnd -or $MsaaProbeWaitSec) {
  if ($MsaaProbeReadyFile) {
    [IO.File]::WriteAllText($MsaaProbeReadyFile, "$PID", [Text.Encoding]::ASCII)
  }
  $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(1, $MsaaProbeWaitSec))
  $lastProbe = 'exact_windows=0'
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
    if ($wins.Count -gt 1) {
      throw 'AurBridge MSAA probe: the requested process/HWND is ambiguous'
    }
    if ($wins.Count -eq 1) {
      $items = @(AurBridgeMsaaElements $wins[0] @('Button','RadioButton','CheckBox','Pane') 80)
      $primary = @($items | Where-Object {
        $_.type -ceq 'Button' -and $_.key -ceq 'Enter' -and
        (!$MsaaProbePrimaryName -or $_.name -ceq $MsaaProbePrimaryName) -and
        $_.enabled -eq [bool]$MsaaProbePrimaryEnabled
      })
      $buttons = @($items | Where-Object { $_.type -ceq 'Button' })
      $enters = @($buttons | Where-Object { $_.key -ceq 'Enter' })
      $lastProbe = "exact_windows=1 pid=$($items[0].pid) controls=$($items.Count) buttons=$($buttons.Count) enter=$($enters.Count) expected=$($primary.Count)"
      if (-not $MsaaProbeWaitSec -or $primary.Count -eq 1) {
        $items | ConvertTo-Json -Depth 6 -Compress
        exit 0
      }
    }
    Start-Sleep -Milliseconds 50
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "AurBridge MSAA probe: no single expected Enter button appeared on the exact process/HWND ($MsaaProbePrimaryName, enabled=$([bool]$MsaaProbePrimaryEnabled)); $lastProbe"
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
      'windows'  { $res.windows = @(TopWindows) }
      'elements' {
        $max = 400; if ($c.max) { $max = [int]$c.max }
        $res.elements = @(Elements $c.match @($c.types) $max)
      }
      'run'      { $res.output = (Invoke-Expression $c.script 2>&1 | Out-String -Width 220) }
      default    { $res.ok = $false; $res.error = "unknown op $($c.op)" }
    }
  } catch { $res.ok = $false; $res.error = "$_" }
  $res.ms = [int]((Get-Date) - $t0).TotalMilliseconds
  try { Post "/e2e/result?id=$($c.id)" $res } catch { }
}
