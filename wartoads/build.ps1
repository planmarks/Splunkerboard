# Build WORMs MICRO for Seeed XIAO ESP32C3
$ErrorActionPreference = "Stop"
$cli = "$env:LOCALAPPDATA\arduino-cli\arduino-cli.exe"
if (-not (Test-Path $cli)) {
  $found = Get-Command arduino-cli -ErrorAction SilentlyContinue
  if ($found) { $cli = $found.Source } else { throw "arduino-cli not found" }
}
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null
& $cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --output-dir "$root\build" $root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "OK  $root\build\wartoads.ino.bin"
Write-Host "Merged image: $root\build\wartoads.ino.merged.bin"
