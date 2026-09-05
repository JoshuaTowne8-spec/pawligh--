param(
    [int]$Port = 8080
)

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot
$envFile = Join-Path $repoRoot "gateway\.env"
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $envFile)) {
    throw "Missing gateway\.env. Copy gateway\.env.example and fill in the API key and device token."
}
if (-not (Test-Path -LiteralPath $python)) {
    throw "Missing .venv. Create the Python virtual environment and install gateway\requirements.txt first."
}

foreach ($line in Get-Content -LiteralPath $envFile) {
    $trimmed = $line.Trim()
    if (-not $trimmed -or $trimmed.StartsWith("#") -or -not $trimmed.Contains("=")) {
        continue
    }
    $parts = $trimmed.Split("=", 2)
    [Environment]::SetEnvironmentVariable($parts[0].Trim(), $parts[1].Trim(), "Process")
}

if (-not $env:DASHSCOPE_API_KEY -or -not $env:DEVICE_TOKEN) {
    throw "DASHSCOPE_API_KEY or DEVICE_TOKEN is missing from gateway\.env."
}

Set-Location -LiteralPath $repoRoot
Write-Host "Emotion gateway: http://0.0.0.0:$Port"
Write-Host "Press Ctrl+C to stop."
& $python -m uvicorn gateway.app.main:app --host 0.0.0.0 --port $Port
