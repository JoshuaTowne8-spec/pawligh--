$ErrorActionPreference = "Stop"
$webDir = Join-Path $PSScriptRoot "pawlight-memories"

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "Node.js 22 or newer is required."
}
if (-not (Get-Command pnpm -ErrorAction SilentlyContinue)) {
    throw "pnpm is required."
}
if (-not (Test-Path -LiteralPath (Join-Path $webDir "node_modules"))) {
    throw "Web dependencies are missing. Run: cd pawlight-memories; pnpm install --frozen-lockfile"
}

Set-Location -LiteralPath $webDir
Write-Host "PawLight web: http://localhost:3000"
Write-Host "Press Ctrl+C to stop."
& pnpm exec vinext dev
