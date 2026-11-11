param(
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Preset = "win64-" + $Config.ToLower()

Write-Host "[repsp] Configuring ($Preset)..." -ForegroundColor Cyan
cmake --preset $Preset -S $Root

Write-Host "[repsp] Building ($Config)..." -ForegroundColor Cyan
cmake --build "$Root/build/$Preset" --config $Config

Write-Host "[repsp] Running tests ($Config)..." -ForegroundColor Cyan
ctest --test-dir "$Root/build/$Preset" -C $Config --output-on-failure
