@echo off
chcp 1251 >nul
setlocal
cd /d "%~dp0"
title AM Graph Viewer

set "EXE="
for /f "delims=" %%F in ('dir /b /a-d "%~dp0AMGraphViewer-v*-win-x64.exe" 2^>nul') do set "EXE=%~dp0%%F"

if exist "%EXE%" (
  start "" "%EXE%"
  exit /b 0
)

echo [!] Graphical viewer not found in this folder.
echo     Expected file: AMGraphViewer-v0.11.0-win-x64.exe
echo.
echo     If you are building from source, run:
echo     powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
echo.
pause
exit /b 1
