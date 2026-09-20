#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?usage: build-menu-controller.sh EMPTY_OUTPUT host|mips}
mode=${2:-mips}
case "$out" in /tmp/gkd-mini-public/gkd-menu-controller-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
[ ! -e "$out" ]; mkdir -m 700 "$out"
python3 "$lane/scripts/generate-cjk-psf2.py" --output "$out/native-cn.psf"
python3 "$lane/scripts/generate-cjk-psf2.py" --size 12 --output "$out/native-cn-12.psf"
python3 "$lane/tests/test_settings_font.py" "$out/native-cn.psf" "$out/native-cn-12.psf"
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$out:/out:rw" \
 -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro "$image" sh -eu -c '
 mode=$1; cc=cc; link="-Wl,--gc-sections"
 if [ "$mode" = mips ]; then cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real; link="$link -static"; fi
 flags="-std=c99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -ffunction-sections -fdata-sections"
 "$cc" $flags -DGKD_APPLICATION_UI=1 -I/lane/include /lane/source/gkd-application-menu.c \
  /lane/source/gkd-menu-state.c /lane/source/gkd-settings-state.c /lane/source/gkd-settings-client.c /lane/source/gkd-ui-language.c /lane/source/gkd-input-owner.c /lane/source/gkd-menu-guard.c \
  /lane/source/gkd-ui.c /lane/source/gkd-ui-plane-client.c $link -o /out/gkd-application-menu
 "$cc" $flags -I/lane/include /lane/source/gkd-menu-state.c /lane/tests/menu_state_fixture.c $link -o /out/menu-state-fixture
 if [ "$mode" = host ]; then
  /out/menu-state-fixture
  for sanitize in normal sanitized; do
   extra=; if [ "$sanitize" = sanitized ]; then extra="-O1 -g -fsanitize=address,undefined"; fi
   "$cc" $flags $extra -I/lane/include /lane/source/gkd-menu-state.c \
    /lane/source/gkd-settings-state.c /lane/tests/settings_state_fixture.c $link -o /out/settings-state-$sanitize
   /out/settings-state-$sanitize
   "$cc" $flags $extra -I/lane/include /lane/source/gkd-settings-client.c \
    /lane/tests/settings_client_fixture.c $link -o /out/settings-client-$sanitize
  done
  for sanitize in normal sanitized; do
   extra=; if [ "$sanitize" = sanitized ]; then extra="-O1 -g -fsanitize=address,undefined"; fi
   "$cc" $flags $extra -DGKD_APPLICATION_UI=1 -Dmain=gkd_application_menu_main -I/lane/include \
    -c /lane/source/gkd-application-menu.c -o /out/runner-$sanitize.o
   "$cc" $flags $extra -D_DEFAULT_SOURCE -DGKD_APPLICATION_UI=1 -I/lane/include \
    /lane/source/gkd-menu-state.c /lane/source/gkd-settings-state.c /lane/source/gkd-ui-language.c /lane/source/gkd-ui-plane-client.c /lane/tests/application_menu_fixture.c \
    /out/runner-$sanitize.o -Wl,--wrap=clock_gettime -Wl,--wrap=open -Wl,--wrap=fstat \
    -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=poll -Wl,--wrap=__poll_chk -Wl,--wrap=read -Wl,--wrap=__read_chk $link -o /out/menu-runner-$sanitize
   /out/menu-runner-$sanitize
  done
  "$cc" $flags -O1 -g -fsanitize=address,undefined -I/lane/include \
   /lane/source/gkd-menu-state.c /lane/tests/menu_state_fixture.c -o /out/menu-state-sanitized
  /out/menu-state-sanitized
  "$cc" $flags -DGKD_INPUT_ROOT=\"/tmp/gkd-input-owner-events\" -DGKD_INPUT_OWNER_DIR=\"/tmp/gkd-input-owner-state\" \
   -I/lane/include /lane/source/gkd-input-owner.c /lane/tests/input_owner_menu_fixture.c \
   -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=fstat -Wl,--wrap=lstat -Wl,--wrap=ioctl -Wl,--wrap=usleep \
   $link -o /out/input-owner-menu-fixture
  /out/input-owner-menu-fixture
 fi
 # Runtime code/data are unchanged; omit non-loadable linker symbols from A.
 if [ "$mode" = mips ]; then
  /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip --strip-unneeded /out/gkd-application-menu
 fi
 cd /out; sha256sum gkd-application-menu menu-state-fixture native-cn.psf native-cn-12.psf >SHA256SUMS
 ' sh "$mode"
if [ "$mode" = host ]; then
 for variant in normal sanitized; do
  docker run --rm --network none --user 0:0 -v "$out:/out:ro" "$image" "/out/settings-client-$variant"
 done
fi
if [ "$mode" = mips ]; then
 file "$out/gkd-application-menu" | grep -q 'ELF 32-bit LSB executable, MIPS'
 ! readelf -l "$out/gkd-application-menu" | grep -q INTERP
fi
printf 'GKD_MENU_CONTROLLER_BUILD_PASS mode=%s\n' "$mode"
