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
  log_viewer.c \
  "$LIBGCC" \
  -o log_viewer.elf
xtensa-esp32s3-elf-strip --strip-all --remove-section=.xt.prop -o log_viewer.xtensa.elf log_viewer.elf
echo "OK: log_viewer.xtensa.elf"
