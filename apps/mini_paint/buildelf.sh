#!/usr/bin/env bash
set -e
LIBGCC="$(xtensa-esp32s3-elf-gcc -print-libgcc-file-name)"
xtensa-esp32s3-elf-gcc \
  -O2 \
  -DESP_PLATFORM \
  -I local_include \
  -Dmain=app_main \
  -nostartfiles -nostdlib \
  -fPIC -shared \
  -fvisibility=hidden \
  -Wl,-e,app_main \
  -Wl,--gc-sections \
  mini_paint.c \
  "$LIBGCC" \
  -o mini_paint.elf
xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o mini_paint.xtensa.elf mini_paint.elf
echo "OK: mini_paint.xtensa.elf"
