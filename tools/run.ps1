<#
.SYNOPSIS
  Build, flash, and monitor the Weather-EPS32S3 firmware - or, with -Dev,
  skip hardware entirely and run a local web server that mirrors the
  setup/debug portal's website for UI iteration. See docs/installation.md.

.EXAMPLE
  ./tools/run.ps1                       # build + upload + monitor (headless env)
  ./tools/run.ps1 -Env panel-acep730    # Waveshare 7.3" ACeP (7-color)
  ./tools/run.ps1 -Env panel-mono-v2    # Waveshare 7.5" e-Paper V2 (monochrome)
  ./tools/run.ps1 -Env panel-inky       # Pimoroni Inky Impression 7.3" (7-color, raw SPI)
  ./tools/run.ps1 -UploadPort COM5      # pin the serial port instead of autodetecting
  ./tools/run.ps1 -Dev                  # local website only, no hardware, http://localhost:8080
  ./tools/run.ps1 -Dev -Port 8081
#>
param(
    [switch]$Dev,
    [ValidateSet("headless", "panel-acep730", "panel-mono-v2", "panel-inky")]
    [string]$Env = "headless",
    [int]$Port = 8080,
    [string]$UploadPort
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Test-PlatformIO {
    python -m platformio --version *> $null
    return $LASTEXITCODE -eq 0
}

if ($Dev) {
    Write-Host "Starting local dev web server (no hardware needed)..." -ForegroundColor Cyan
    python "$RepoRoot/tools/dev_server.py" --port $Port
    exit $LASTEXITCODE
}

if (-not (Test-PlatformIO)) {
    Write-Host "PlatformIO not found - installing via pip..." -ForegroundColor Yellow
    python -m pip install --user platformio
    if (-not (Test-PlatformIO)) {
        Write-Error "PlatformIO install failed - install it manually (pip install platformio) and retry."
        exit 1
    }
}

$envName = if ($Env -eq "headless") { "esp32-s3-devkitc-1" } else { "esp32-s3-devkitc-1-$Env" }

Write-Host "Building environment '$envName'..." -ForegroundColor Cyan
python -m platformio run -e $envName
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to board..." -ForegroundColor Cyan
$uploadArgs = @("-e", $envName, "-t", "upload")
if ($UploadPort) { $uploadArgs += @("--upload-port", $UploadPort) }
python -m platformio run @uploadArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Flashed. If this is a factory-fresh board (no WiFi saved yet) or you're" -ForegroundColor Green
Write-Host "holding the BOOT button, it will come up as an access point:" -ForegroundColor Green
Write-Host "  connect to WiFi ""WeatherEPS32S3-Setup"", then browse to http://192.168.4.1" -ForegroundColor Green
Write-Host ""
Write-Host "Opening serial monitor (Ctrl+C to exit)..." -ForegroundColor Cyan
$monitorArgs = @("device", "monitor", "-e", $envName)
if ($UploadPort) { $monitorArgs += @("--port", $UploadPort) }
python -m platformio @monitorArgs
