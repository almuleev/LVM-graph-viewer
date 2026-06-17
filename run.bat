@echo off
chcp 1251 >nul
setlocal
cd /d "%~dp0"
title LVM Reader

set "EXE=%~dp0lvm_reader.exe"

if not exist "%EXE%" (
  echo [!] lvm_reader.exe не найден в этой папке.
  echo     Сначала соберите программу ^(см. README.md^):
  echo     g++ -std=c++17 -O2 -static -o lvm_reader.exe main.cpp lvm_parser.cpp fft.cpp analysis.cpp
  echo.
  pause
  exit /b 1
)

rem Файл можно перетащить на этот .bat (придёт как %1) либо ввести вручную.
set "FILE=%~1"
if "%FILE%"=="" (
  echo Перетащите .lvm / .txt файл на run.bat,
  echo либо введите путь к файлу и нажмите Enter:
  echo.
  set /p "FILE=Файл: "
)

rem Убрать кавычки, если путь вставлен в кавычках.
set "FILE=%FILE:"=%"

if "%FILE%"=="" (
  echo.
  echo [!] Файл не указан.
  echo.
  pause
  exit /b 1
)

if not exist "%FILE%" (
  echo.
  echo [!] Файл не найден: %FILE%
  echo.
  pause
  exit /b 1
)

echo.
echo ============================================================
echo  Анализ файла: %FILE%
echo ============================================================
echo.
"%EXE%" "%FILE%" --info --stats --fft
echo.
echo ------------------------------------------------------------
echo  Готово. Все опции: запустите  lvm_reader.exe --help
echo  (экспорт в CSV: добавьте  --csv result.csv)
echo ------------------------------------------------------------
echo.
pause
endlocal
