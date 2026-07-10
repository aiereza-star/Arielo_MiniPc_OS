#!/usr/bin/env bash
set -e

LIBGCC="$(xtensa-esp32s3-elf-gcc -print-libgcc-file-name)"
echo "Usando libgcc: $LIBGCC"

xtensa-esp32s3-elf-gcc \
  -O2 \
  -mlongcalls \
  -DESP_PLATFORM \
  -I local_include \
  -Dmain=app_main \
  -nostartfiles -nostdlib \
  -fPIC -shared \
  -fvisibility=hidden \
  -Wl,-e,app_main \
  -Wl,--gc-sections \
  sysinfo.c \
  "$LIBGCC" \
  -o sysinfo.elf

xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o sysinfo.xtensa.elf sysinfo.elf

echo "OK: generado sysinfo.xtensa.elf"
