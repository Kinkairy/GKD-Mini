#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
[ "$#" -eq 3 ] || { echo 'usage: build-settings-preview.sh EMPTY_OUTPUT FONT_PSF host|mips' >&2; exit 2; }
out=$1; font=$2; mode=$3
case "$out" in /tmp/gkd-mini-public/gkd-settings-preview-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
[ -f "$font" ]; [ ! -e "$out" ]; mkdir -m 700 "$out"
cp -- "$font" "$out/fallback.psf"
python3 "$lane/scripts/generate-cjk-psf2.py" --output "$out/native-cn.psf"
python3 "$lane/scripts/generate-cjk-psf2.py" --size 12 --output "$out/native-cn-12.psf"
python3 "$lane/tests/test_settings_font.py" "$out/native-cn.psf" "$out/native-cn-12.psf"
docker run --rm --network none --user "$(id -u):$(id -g)" \
 -v "$lane:/lane:ro" -v "$out:/out:rw" \
 -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro "$image" sh -eu -c '
 mode=$1; cc=cc; link=
 variants="normal sanitized"
 if [ "$mode" = mips ]; then cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real; link=-static; variants=normal; fi
 flags="-std=c99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion"
 for variant in $variants; do
  extra=; if [ "$variant" = sanitized ]; then extra="-O1 -g -fsanitize=address,undefined"; fi
  "$cc" $flags $extra -DGKD_APPLICATION_UI=1 -I/lane/include /lane/source/gkd-ui.c \
   /lane/source/gkd-ui-language.c /lane/tests/settings_render_fixture.c $link -o /out/render-$variant
  "$cc" $flags $extra -DGKD_APPLICATION_UI=1 -I/lane/include /lane/tests/native_font_fixture.c $link -o /out/native-font-$variant
  "$cc" $flags $extra -I/lane/include /lane/source/gkd-menu-state.c \
   /lane/source/gkd-settings-state.c /lane/tests/settings_state_fixture.c $link -o /out/state-$variant
  if [ "$mode" = host ]; then
   /out/state-$variant
   /out/native-font-$variant /out/fallback.psf /out/native-cn.psf /out/native-cn-12.psf
   /out/render-$variant /out/fallback.psf /out /out/native-cn.psf /out/native-cn-12.psf
  fi
 done
 cd /out; sha256sum fallback.psf native-cn.psf native-cn-12.psf render-* state-* native-font-* > SHA256SUMS
 ' sh "$mode"
printf 'GKD_SETTINGS_PREVIEW_BUILD_PASS mode=%s\n' "$mode"
