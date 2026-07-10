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
  sheet.c \
  "$LIBGCC" \
  -o sheet.elf

xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o sheet.xtensa.elf sheet.elf

echo "OK: generado sheet.xtensa.elf"
