@echo off
REM ============================================================
REM buildelf.bat - Compila pong.c como app ELF para BreezyBox
REM
REM Requiere el mismo compilador que usas para el firmware
REM (xtensa-esp32s3-elf-gcc de tu ESP-IDF v5.5.4). Ejecuta este
REM script DESDE la misma consola PowerShell/CMD donde activas
REM ESP-IDF (el mismo paso que usas antes de "idf.py build"),
REM asi el compilador ya esta en el PATH.
REM
REM Uso:
REM   1) Abre PowerShell y activa ESP-IDF como siempre.
REM   2) cd a la carpeta donde tengas pong.c y local_include\
REM   3) Ejecuta:  buildelf.bat
REM   4) Copia pong.xtensa.elf (o pong.elf) a /sdcard o /usb
REM      y lanzalo desde APPS Launcher.
REM ============================================================

where xtensa-esp32s3-elf-gcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: xtensa-esp32s3-elf-gcc no esta en el PATH.
    echo Activa primero el entorno ESP-IDF ^(el mismo paso que usas
    echo antes de "idf.py build"^), y vuelve a ejecutar este script.
    exit /b 1
)

for /f "delims=" %%i in ('xtensa-esp32s3-elf-gcc -print-libgcc-file-name') do set LIBGCC=%%i
echo Usando libgcc: %LIBGCC%

xtensa-esp32s3-elf-gcc ^
  -O2 ^
  -DESP_PLATFORM ^
  -I local_include ^
  -Dmain=app_main ^
  -nostartfiles -nostdlib ^
  -fPIC -shared ^
  -fvisibility=hidden ^
  -Wl,-e,app_main ^
  -Wl,--gc-sections ^
  pong.c ^
  "%LIBGCC%" ^
  -o pong.elf

if errorlevel 1 (
    echo ERROR: fallo la compilacion.
    exit /b 1
)

xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o pong.xtensa.elf pong.elf

if errorlevel 1 (
    echo ERROR: fallo el strip. pong.elf sin recortar puede seguir sirviendo.
    exit /b 1
)

echo.
echo OK: generado pong.xtensa.elf
echo Copialo a la SD/USB y lanzalo desde APPS Launcher, o desde
echo la terminal con: pong.xtensa.elf
