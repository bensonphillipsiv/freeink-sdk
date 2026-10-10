#!/bin/sh
set -eu
cd "$(dirname "$0")"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/freeink-imu.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
for mode in enabled disabled glyph; do
  flags='-DFREEINK_DEVICE_METALIO_EINK4=1'
  test=test_sc7a20h.cpp
  if [ "$mode" = disabled ]; then flags="$flags -DFREEINK_CAP_IMU=0"; fi
  if [ "$mode" = glyph ]; then flags='-DFREEINK_DEVICE_GLYPH=1'; test=test_lis2dw12.cpp; fi
  c++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-private-field -Wno-unused-function -Wno-unused-const-variable $flags \
    -Istubs -I../../../InputManager/test/host/metalio_stubs \
    -I../../../BoardConfig/include -I../../include "$test" ../../src/Imu.cpp -o "$BUILD_DIR/$mode"
  "$BUILD_DIR/$mode"
done
