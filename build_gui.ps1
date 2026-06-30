# Build the native Win32 GUI viewer (AMGraphViewer-vX.X.X-win-x64.exe) with MSYS2/MinGW g++.
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

$outName = "AMGraphViewer-$version-win-x64.exe"
$versionDefine = '-DAPP_VERSION_W=L\"' + $version + '\"'
$resourceObj = "AM_logo_res.o"

if (-not (Test-Path -LiteralPath "AM_logo.rc")) {
    throw "Required file not found: AM_logo.rc"
}
if (-not (Test-Path -LiteralPath "AM_logo.ico")) {
    throw "Required file not found: AM_logo.ico"
}

Write-Host "windres -O coff -i AM_logo.rc -o $resourceObj"
& windres -O coff -i AM_logo.rc -o $resourceObj
if ($LASTEXITCODE -ne 0) {
    Write-Host "Resource compilation failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

$flags = @(
    "-std=c++17", "-O2", "-finput-charset=UTF-8", "-municode", "-static", "-mwindows",
    $versionDefine,
    "-o", $outName,
    $resourceObj,
    "gui_main.cpp", "gap_details.cpp", "lvm_parser.cpp", "fft.cpp", "analysis.cpp", "export_helpers.cpp", "formula_engine.cpp",
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
