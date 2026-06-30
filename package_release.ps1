param(
    [string]$OutputDir = "dist\LVM-graph-viewer",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

$version = git describe --tags --abbrev=0 2>$null
if (-not $version) { $version = "v0.0.0" }
$guiExe = "LVM-graph-viewer-$version-win-x64.exe"
$zipName = "LVM-graph-viewer-$version-win-x64.zip"

$files = @(
    $guiExe,
    "lvm_reader.exe",
    "Start GUI.bat",
    "run.bat",
    "README.md",
    "README_EN.md",
    "README_RU.md",
    "LICENSE"
)

foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Required file not found: $file"
    }
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

foreach ($file in $files) {
    Copy-Item -LiteralPath $file -Destination $OutputDir -Force
}

if ($Zip) {
    $zipPath = Join-Path (Split-Path $OutputDir -Parent) $zipName
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $OutputDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Created $zipPath"
} else {
    Write-Host "Prepared $OutputDir"
}
