param(
    [Parameter(Position = 0)]
    [string]$Port = "COM5"
)

$ErrorActionPreference = "Stop"
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "idf.py was not found. Run this script inside ESP-IDF PowerShell."
}

$firmwareDir = Join-Path $PSScriptRoot "firmware"
Push-Location -LiteralPath $firmwareDir
try {
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        throw "Firmware build failed."
    }
    idf.py -p $Port flash monitor
} finally {
    Pop-Location
}
