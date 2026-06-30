param(
    [string]$OutputDir = "dist\LVM-graph-viewer",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

$files = @(
    "LVM-graph-viewer-win-x64.exe",
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
    $zipPath = Join-Path (Split-Path $OutputDir -Parent) "LVM-graph-viewer-win-x64.zip"
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $OutputDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Created $zipPath"
} else {
    Write-Host "Prepared $OutputDir"
}
