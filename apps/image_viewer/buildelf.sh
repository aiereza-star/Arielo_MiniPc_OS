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
  image_viewer.c \
  "$LIBGCC" \
  -o image_viewer.elf
xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o image_viewer.xtensa.elf image_viewer.elf
echo "OK: image_viewer.xtensa.elf"
