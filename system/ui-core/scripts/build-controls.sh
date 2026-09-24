#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?usage: build-controls.sh EMPTY_OUTPUT [host|mips]}
mode=${2:-mips}
image=local/c-builder:2026.08.02-kernel
image_id=sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1
case "$out" in /tmp/gkd-mini-public/gkd-controls-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "$image_id" ]
mkdir -m 700 "$out"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$out:/out:rw" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  "$image" sh -eu -c '
    mode=$1
    cc=cc
    link="-Wl,--gc-sections"
    if [ "$mode" = mips ]; then
      cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
      link="$link -static"
    fi
    flags="-std=c99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -ffunction-sections -fdata-sections"
    "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include \
      /lane/source/gkd-controls.c /lane/source/gkd-controls-state.c \
      /lane/source/gkd-controls-hardware.c /lane/source/gkd-input-owner.c \
      /lane/source/gkd-hardware-state.c /lane/source/gkd-menu-guard.c \
      /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c \
      $link -o /out/gkd-controls
    "$cc" $flags -I/lane/include /lane/source/gkd-controls-state.c \
      /lane/tests/controls_state_fixture.c $link -o /out/controls-state-fixture
    "$cc" $flags -I/lane/include /lane/source/gkd-controls-hardware.c \
      /lane/tests/controls_hardware_fixture.c -Wl,--wrap=ioctl \
      -Wl,--wrap=pread -Wl,--wrap=__pread_chk -Wl,--wrap=pwrite $link -o /out/controls-hardware-fixture
    "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include /lane/source/gkd-controls-state.c \
      /lane/tests/controls_runner_fixture.c -Wl,--wrap=read -Wl,--wrap=__read_chk \
      -Wl,--wrap=pread -Wl,--wrap=__pread_chk -Wl,--wrap=ioctl $link -o /out/controls-runner-fixture
    "$cc" $flags -I/lane/include /lane/source/gkd-hardware-state.c \
      /lane/tests/hardware_state_fixture.c $link -o /out/hardware-state-fixture
    "$cc" $flags -I/lane/include /lane/tests/menu_guard_fixture.c $link -o /out/menu-guard-fixture
    if [ "$mode" = host ]; then
      /out/controls-runner-fixture
      "$cc" $flags -O1 -g -fsanitize=address,undefined -DGKD_APPLICATION_UI=1 -I/lane/include \
        /lane/source/gkd-controls-state.c /lane/tests/controls_runner_fixture.c \
        -Wl,--wrap=read -Wl,--wrap=__read_chk -Wl,--wrap=pread -Wl,--wrap=__pread_chk \
        -Wl,--wrap=ioctl -o /out/controls-runner-sanitized
      /out/controls-runner-sanitized
      /out/hardware-state-fixture
      /out/menu-guard-fixture
      "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
        /lane/source/gkd-hardware-state.c /lane/tests/hardware_state_fixture.c -o /out/hardware-state-sanitized
      /out/hardware-state-sanitized
      "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
        /lane/tests/menu_guard_fixture.c -o /out/menu-guard-sanitized
      /out/menu-guard-sanitized
      /out/controls-state-fixture
      /out/controls-hardware-fixture
      for part in state hardware; do
        wraps=
        if [ "$part" = hardware ]; then wraps="-Wl,--wrap=ioctl -Wl,--wrap=pread -Wl,--wrap=__pread_chk -Wl,--wrap=pwrite"; fi
        "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
          /lane/source/gkd-controls-$part.c /lane/tests/controls_${part}_fixture.c \
          $wraps -o /out/controls-$part-sanitized
        /out/controls-$part-sanitized
      done
    fi
    if [ "$mode" = mips ]; then
      /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip --strip-unneeded /out/gkd-controls
    fi
    cp /lane/device/gkd-controls-start /out/gkd-controls-start
    cd /out
    sha256sum gkd-controls gkd-controls-start controls-state-fixture controls-hardware-fixture controls-runner-fixture hardware-state-fixture menu-guard-fixture >SHA256SUMS
  ' sh "$mode"
if [ "$mode" = mips ]; then
  file "$out/gkd-controls" | grep -q 'ELF 32-bit LSB executable, MIPS'
  ! readelf -l "$out/gkd-controls" | grep -q INTERP
fi
printf 'GKD_CONTROLS_BUILD=PASS mode=%s output=%s\n' "$mode" "$out"
