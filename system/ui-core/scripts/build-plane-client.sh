#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?usage: build-plane-client.sh EMPTY_OUTPUT [host|mips]}
mode=${2:-mips}
image=local/c-builder:2026.08.02-kernel
image_id=sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1
toolchain=/srv/c-builder/toolchains/gkdmini-gcw0-d28
case "$out" in /tmp/gkd-mini-public/gkd-ui-plane-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "$image_id" ]
mkdir -m 700 "$out"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$out:/out:rw" -v "$toolchain:/opt/toolchain:ro" \
  "$image" sh -eu -c '
    mode=$1
    cc=cc
    if [ "$mode" = mips ]; then
      cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
    fi
    flags="-std=c99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -ffunction-sections -fdata-sections"
    link="-Wl,--gc-sections"
    if [ "$mode" = mips ]; then link="$link -static"; fi
    "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include \
      /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c \
      /lane/source/gkd-ui-plane-probe.c $link -o /out/gkd-ui-plane-probe
    "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include \
      /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c \
      /lane/source/gkd-ui-menu-probe.c $link -o /out/gkd-ui-menu-probe
    "$cc" $flags -I/lane/include /lane/source/gkd-ui-plane-client.c \
      /lane/tests/plane_client_fixture.c -Wl,--wrap=ioctl $link \
      -o /out/plane-client-fixture
    "$cc" $flags -I/lane/include /lane/source/gkd-ui-plane-client.c \
      /lane/tests/menu_client_fixture.c -Wl,--wrap=ioctl $link \
      -o /out/menu-client-fixture
    if [ "$mode" = host ]; then
      /out/plane-client-fixture
      /out/menu-client-fixture
      "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
        /lane/source/gkd-ui-plane-client.c /lane/tests/plane_client_fixture.c \
        -Wl,--wrap=ioctl -o /out/plane-client-sanitized
      /out/plane-client-sanitized
      "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
        /lane/source/gkd-ui-plane-client.c /lane/tests/menu_client_fixture.c \
        -Wl,--wrap=ioctl -o /out/menu-client-sanitized
      /out/menu-client-sanitized
      "$cc" $flags -DGKD_APPLICATION_UI=1 -Dmain=gkd_ui_menu_probe_main \
        -I/lane/include -c /lane/source/gkd-ui-menu-probe.c \
        -o /out/menu-probe-test.o
      "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include \
        /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c \
        /lane/tests/menu_probe_fixture.c /out/menu-probe-test.o \
        -Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=close -Wl,--wrap=ioctl \
        -Wl,--wrap=poll $link -o /out/menu-probe-fixture
      /out/menu-probe-fixture
      "$cc" $flags -O1 -g -fsanitize=address,undefined -DGKD_APPLICATION_UI=1 \
        -Dmain=gkd_ui_menu_probe_main -I/lane/include -c \
        /lane/source/gkd-ui-menu-probe.c -o /out/menu-probe-sanitized.o
      "$cc" $flags -O1 -g -fsanitize=address,undefined -DGKD_APPLICATION_UI=1 \
        -I/lane/include /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c \
        /lane/tests/menu_probe_fixture.c /out/menu-probe-sanitized.o \
        -Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=close -Wl,--wrap=ioctl \
        -Wl,--wrap=poll $link -o /out/menu-probe-sanitized
      /out/menu-probe-sanitized
      python3 /lane/tests/test_menu_preview.py -q
      printf "%s\n" \
        "GKD_UI_PLANE_CLIENT=PASS ABI32/validation/one-call/errors/no-fallback" \
        "GKD_UI_MENU_CLIENT=PASS ABI32/caps/reserved/validation/one-call/errors/no-fallback" \
        "GKD_UI_MENU_SANITIZER=PASS address,undefined" \
        "GKD_UI_MENU_PROBE=PASS render/open/caps/submit/normal/sigterm-clear" \
        "GKD_UI_MENU_PROBE_SANITIZER=PASS address,undefined" \
        "GKD_UI_MENU_PREVIEW=PASS enabled/disabled/missing/invalid/getter-failure" > /out/TEST-RESULTS.txt
    else
      printf "%s\n" \
        "GKD_UI_PLANE_CLIENT_FIXTURE=BUILT static-mips" \
        "GKD_UI_MENU_CLIENT_FIXTURE=BUILT static-mips" \
        "GKD_UI_MENU_PROBE=BUILT static-mips" > /out/TEST-RESULTS.txt
    fi
    cp /lane/device/gkd-ui-menu-preview /out/gkd-ui-menu-preview
    chmod 0555 /out/gkd-ui-menu-preview
    printf "%s\n" \
      "GKD_UI_MENU_CONCERN=no device execution or live ABI acceptance in this build" \
      "GKD_UI_MENU_CONCERN=probe is an explicit display-only fixture and is not packaged or invoked" \
      > /out/CONCERNS.txt
    cd /out
    sha256sum gkd-ui-plane-probe gkd-ui-menu-probe gkd-ui-menu-preview \
      plane-client-fixture menu-client-fixture >SHA256SUMS
  ' sh "$mode"
if [ "$mode" = mips ]; then
  file "$out/gkd-ui-plane-probe" | grep -q 'ELF 32-bit LSB executable, MIPS'
  ! readelf -l "$out/gkd-ui-plane-probe" | grep -q INTERP
  file "$out/gkd-ui-menu-probe" | grep -q 'ELF 32-bit LSB executable, MIPS'
  ! readelf -l "$out/gkd-ui-menu-probe" | grep -q INTERP
fi
printf 'GKD_UI_PLANE_CLIENT_BUILD=PASS mode=%s output=%s\n' "$mode" "$out"
