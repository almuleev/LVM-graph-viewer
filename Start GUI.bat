@echo off
chcp 1251 >nul
setlocal
cd /d "%~dp0"
title LVM Graph Viewer

set "EXE="
for /f "delims=" %%F in ('dir /b /a-d "%~dp0LVM-graph-viewer-v*-win-x64.exe" 2^>nul') do set "EXE=%~dp0%%F"

if exist "%EXE%" (
  start "" "%EXE%"
  exit /b 0
)

echo [!] Graphical viewer not found in this folder.
echo     Expected file: LVM-graph-viewer-vX.X.X-win-x64.exe
echo.
echo     If you are building from source, run:
echo     powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
echo.
pause
exit /b 1
