# Flash WORMs MICRO. Usage: .\flash.ps1 COM5
$ErrorActionPreference = "Stop"
if ($args.Count -lt 1) {
  Write-Host "Usage: .\flash.ps1 COMx"
  Write-Host "Ports:"
  $cliProbe = "$env:LOCALAPPDATA\arduino-cli\arduino-cli.exe"
  if (Test-Path $cliProbe) { & $cliProbe board list }
  exit 1
}
$port = $args[0]
$cli = "$env:LOCALAPPDATA\arduino-cli\arduino-cli.exe"
if (-not (Test-Path $cli)) {
  $found = Get-Command arduino-cli -ErrorAction SilentlyContinue
  if ($found) { $cli = $found.Source } else { throw "arduino-cli not found" }
}
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
if (-not (Test-Path "$root\build\wartoads.ino.bin")) {
  & "$root\build.ps1"
}
& $cli upload -p $port --fqbn esp32:esp32:XIAO_ESP32C3 --input-dir "$root\build"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Flashed $port"
# Native USB on XIAO C3 often stays in download mode after esptool hard_reset.
Start-Sleep -Milliseconds 900
try {
  $sp = New-Object System.IO.Ports.SerialPort $port, 1200, None, 8, One
  $sp.DtrEnable = $false
  $sp.RtsEnable = $true
  $sp.ReadTimeout = 200
  $sp.Open()
  Start-Sleep -Milliseconds 80
  $sp.DtrEnable = $true
  $sp.RtsEnable = $false
  Start-Sleep -Milliseconds 120
  $sp.Close()
  $sp.Dispose()
  Write-Host "Reset $port"
} catch {
  Write-Host "If the board did not reboot, tap RESET."
}
