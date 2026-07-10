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
  breakout.c \
  "$LIBGCC" \
  -o breakout.elf
xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o breakout.xtensa.elf breakout.elf
echo "OK: breakout.xtensa.elf"
