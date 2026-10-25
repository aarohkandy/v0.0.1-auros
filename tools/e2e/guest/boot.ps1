# Started hidden at every logon (boot.vbs, from the 'E2E helper' logon task). Fetches the
# UI helper from the test's host and runs it, so the helper can change
# without reinstalling Windows; outside the test nothing answers, and
# this waits quietly.
$hub = 'http://10.0.2.2:8000'
while ($true) {
  try {
    $code = (Invoke-WebRequest -UseBasicParsing "$hub/e2e/helper.ps1" -TimeoutSec 15).Content
    if ($code -is [byte[]]) { $code = [Text.Encoding]::UTF8.GetString($code) }
    if ($code) { break }
  } catch { }
  Start-Sleep 5
}
$helper = 'C:\e2e\helper.ps1'
[IO.File]::WriteAllText($helper, $code, [Text.UTF8Encoding]::new($false))
& powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper
if ($LASTEXITCODE -ne 0) { throw "E2E helper exited $LASTEXITCODE" }
