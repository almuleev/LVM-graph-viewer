@echo off
chcp 1251 >nul
setlocal
cd /d "%~dp0"
title AM Graph Viewer CLI

echo This is the command-line analyzer.
echo For the graphical viewer, launch "Start GUI.bat" or "AMGraphViewer-v0.11.0-win-x64.exe".
echo.

set "EXE=%~dp0lvm_reader.exe"

if not exist "%EXE%" (
  echo [!] CLI version of AM Graph Viewer is not found in this folder.
  echo     Build the CLI first (see README.md):
  echo     g++ -std=c++17 -O2 -static -o lvm_reader.exe main.cpp lvm_parser.cpp fft.cpp analysis.cpp
  echo.
  pause
  exit /b 1
)

rem Drag a .lvm or .txt file onto this .bat (arrives as %%1) or enter a path.
set "FILE=%~1"
if "%FILE%"=="" (
  echo Drag a .lvm / .txt file onto run.bat for CLI analysis,
  echo or type a path and press Enter:
  echo.
  set /p "FILE=File: "
)

rem Remove quotes if the path was pasted with them.
set "FILE=%FILE:"=%"

if "%FILE%"=="" (
  echo.
  echo [!] File not specified.
  echo.
  pause
  exit /b 1
)

if not exist "%FILE%" (
  echo.
  echo [!] File not found: %FILE%
  echo.
  pause
  exit /b 1
)

echo.
echo ============================================================
echo  Analyzing file: %FILE%
echo ============================================================
echo.
"%EXE%" "%FILE%" --info --stats --fft
echo.
echo ------------------------------------------------------------
echo  Done. All CLI options: run lvm_reader.exe --help
echo  CSV export: add --csv result.csv
echo  Graphical mode: Start GUI.bat
echo ------------------------------------------------------------
echo.
pause
endlocal
