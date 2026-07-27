param(
  [string]$DeviceAddress
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$secretsPath = Join-Path $projectRoot "src\secrets.h"

if (-not (Test-Path -LiteralPath $secretsPath)) {
  throw "Missing src\secrets.h. Copy src\secrets.example.h and fill in the OTA settings."
}

$secrets = Get-Content -LiteralPath $secretsPath -Raw
$passwordMatch = [regex]::Match($secrets, 'OTA_PASSWORD\s*=\s*"([^"]+)"')
$hostMatch = [regex]::Match($secrets, 'OTA_HOST\s*=\s*"([^"]+)"')

if (-not $passwordMatch.Success -or [string]::IsNullOrWhiteSpace($passwordMatch.Groups[1].Value)) {
  throw "OTA_PASSWORD is missing from src\secrets.h."
}

if ([string]::IsNullOrWhiteSpace($DeviceAddress)) {
  if (-not $hostMatch.Success -or [string]::IsNullOrWhiteSpace($hostMatch.Groups[1].Value)) {
    throw "OTA_HOST is missing from src\secrets.h."
  }

  $DeviceAddress = $hostMatch.Groups[1].Value
}

$env:SMART_KNOB_OTA_PASSWORD = $passwordMatch.Groups[1].Value
$platformIo = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"

if (-not (Test-Path -LiteralPath $platformIo)) {
  $platformIo = "pio"
}

Write-Host "Uploading Smart Knob firmware over OTA to $DeviceAddress..."
& $platformIo run -e esp32s3_ota -t upload --upload-port $DeviceAddress
exit $LASTEXITCODE
