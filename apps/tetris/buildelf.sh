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
  tetris.c \
  "$LIBGCC" \
  -o tetris.elf
xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o tetris.xtensa.elf tetris.elf
echo "OK: tetris.xtensa.elf"
