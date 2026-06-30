# Build the native Win32 GUI viewer (LVM-graph-viewer-win-x64.exe) with MSYS2/MinGW g++.
# Usage (from this folder):  powershell -ExecutionPolicy Bypass -File build_gui.ps1
$ErrorActionPreference = "Stop"

# Make sure the MSYS2 UCRT64 g++ is on PATH.
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
}

Set-Location $PSScriptRoot

# Extract version from the latest git tag (e.g. v0.4.4).
$version = git describe --tags --abbrev=0 2>$null
if (-not $version) { $version = "v0.0.0" }

$outName = "LVM-graph-viewer-win-x64.exe"
$versionDefine = '-DAPP_VERSION_W=L\"' + $version + '\"'

$flags = @(
    "-std=c++17", "-O2", "-finput-charset=UTF-8", "-municode", "-static", "-mwindows",
    $versionDefine,
    "-o", $outName,
    "gui_main.cpp", "lvm_parser.cpp", "fft.cpp", "analysis.cpp", "export_helpers.cpp", "formula_engine.cpp",
    "-lcomdlg32", "-lgdi32", "-luser32", "-lgdiplus", "-lcomctl32"
)

Write-Host "g++ $($flags -join ' ')"
& g++ @flags

if ($LASTEXITCODE -eq 0 -and (Test-Path $outName)) {
    Write-Host "Built $outName" -ForegroundColor Green
} else {
    Write-Host "Build failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}
