# Build the native Win32 GUI viewer (lvm_viewer_gui.exe) with MSYS2/MinGW g++.
# Usage (from this folder):  powershell -ExecutionPolicy Bypass -File build_gui.ps1
$ErrorActionPreference = "Stop"

# Make sure the MSYS2 UCRT64 g++ is on PATH.
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
}

Set-Location $PSScriptRoot

$flags = @(
    "-std=c++17", "-O2", "-municode", "-static", "-mwindows",
    "-o", "lvm_viewer_gui.exe",
    "gui_main.cpp", "lvm_parser.cpp", "fft.cpp", "analysis.cpp",
    "-lcomdlg32", "-lgdi32", "-luser32", "-lgdiplus", "-lcomctl32"
)

Write-Host "g++ $($flags -join ' ')"
& g++ @flags

if ($LASTEXITCODE -eq 0 -and (Test-Path lvm_viewer_gui.exe)) {
    Write-Host "Built lvm_viewer_gui.exe" -ForegroundColor Green
} else {
    Write-Host "Build failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}
