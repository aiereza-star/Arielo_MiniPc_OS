@echo off
REM ============================================================
REM buildelf.bat - Compila sheet.c como app ELF Arielo MiniPC OS
REM Chasis Calculator/Notepad, app externa.
REM ============================================================

where xtensa-esp32s3-elf-gcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: xtensa-esp32s3-elf-gcc no esta en el PATH.
    echo Activa primero el entorno ESP-IDF.
    exit /b 1
)

for /f "delims=" %%i in ('xtensa-esp32s3-elf-gcc -print-libgcc-file-name') do set LIBGCC=%%i
echo Usando libgcc: %LIBGCC%

xtensa-esp32s3-elf-gcc ^
  -O2 ^
  -mlongcalls ^
  -DESP_PLATFORM ^
  -I local_include ^
  -Dmain=app_main ^
  -nostartfiles -nostdlib ^
  -fPIC -shared ^
  -fvisibility=hidden ^
  -Wl,-e,app_main ^
  -Wl,--gc-sections ^
  sheet.c ^
  "%LIBGCC%" ^
  -o sheet.elf

if errorlevel 1 (
    echo ERROR: fallo la compilacion.
    exit /b 1
)

xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o sheet.xtensa.elf sheet.elf

if errorlevel 1 (
    echo ERROR: fallo el strip. sheet.elf sin recortar puede seguir sirviendo.
    exit /b 1
)

echo.
echo OK: generado sheet.xtensa.elf
