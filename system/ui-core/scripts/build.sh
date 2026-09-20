#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
output=${1:?output directory required}
font_source=${2:?Linux font_8x16.c required}
mode=${3:-host}
profile=${4:-normal}

case "$output" in
  /tmp/gkd-mini-public/gkd-ui-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/ui-*|/out/gkd-ui-*) ;;
  *) echo GKD_UI_BUILD=BLOCKED unsafe-output >&2; exit 2 ;;
esac
[ ! -e "$output" ] && [ -f "$font_source" ] && [ ! -L "$font_source" ]
mkdir -p "$output"
python3 "$lane/scripts/generate-psf2.py" --source "$font_source" --output "$output/fallback.psf"

case "$mode" in
host)
  cc=${CC:-cc}
  flags='-std=c99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion'
  input_flags='-std=gnu99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow'
  ;;
mips-static)
  cc=${GKD_MIPS_CC:-/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real}
  flags='-std=c99 -Os -Wall -Wextra -Werror -Wformat=2 -Wshadow -static -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--build-id=none'
  input_flags='-std=gnu99 -Os -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wl,--build-id=none'
  strip=${GKD_MIPS_STRIP:-/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip}
  ;;
*) echo GKD_UI_BUILD=BLOCKED invalid-mode >&2; exit 2 ;;
esac

case "$profile" in
normal) profile_flags=; ui_sources="$lane/source/gkd-ui.c $lane/source/gkd-recovery-ui.c" ;;
dedicated-r) profile_flags=-DGKD_DEDICATED_RECOVERY=1; ui_sources="$lane/source/gkd-ui.c $lane/source/gkd-recovery-ui.c $lane/source/gkd-input-owner.c $lane/source/gkd-menu-guard.c $lane/source/gkd-r-poweroff.c" ;;
*) echo GKD_UI_BUILD=BLOCKED invalid-profile >&2; exit 2 ;;
esac

$cc $flags $profile_flags -I"$lane/include" $ui_sources -o "$output/gkd-recovery-ui"
chmod 0555 "$output/gkd-recovery-ui"
cp "$lane/config/gdkmini-ui.defaults.conf" "$output/gdkmini-ui.defaults.conf"
if [ "$profile" = dedicated-r ]; then
  # Same schema and loader, separate profile; frozen A defaults stay identical.
  awk -F'|' '$7 == "ui-core" {print $1 "=" $3}' \
    "$lane/../config-core/schema/gdkmini.schema" >"$output/gdkmini-ui.defaults.conf"
fi
chmod 0444 "$output/gdkmini-ui.defaults.conf" "$output/fallback.psf"
if [ "$profile" = dedicated-r ]; then
  $cc $input_flags -I"$lane/include" "$lane/source/gkd-input.c" -o "$output/gkd-input.unstripped"
  $cc $input_flags "$lane/source/gkd-screenshot.c" -o "$output/gkd-screenshot.unstripped"
  if [ "$mode" = mips-static ]; then
    "$strip" -o "$output/gkd-input" "$output/gkd-input.unstripped"
    "$strip" -o "$output/gkd-screenshot" "$output/gkd-screenshot.unstripped"
    rm "$output/gkd-input.unstripped" "$output/gkd-screenshot.unstripped"
  else
    mv "$output/gkd-input.unstripped" "$output/gkd-input"
    mv "$output/gkd-screenshot.unstripped" "$output/gkd-screenshot"
    "$output/gkd-screenshot" --self-test
  fi
  cp "$lane/device/S95gkd-input" "$output/S95gkd-input"
  chmod 0555 "$output/gkd-input" "$output/gkd-screenshot" "$output/S95gkd-input"
  (cd "$output" && sha256sum S95gkd-input fallback.psf gkd-input gkd-recovery-ui gkd-screenshot gdkmini-ui.defaults.conf >SHA256SUMS)
else
  (cd "$output" && sha256sum fallback.psf gkd-recovery-ui gdkmini-ui.defaults.conf >SHA256SUMS)
fi
printf 'GKD_UI_BUILD=PASS mode=%s output=%s\n' "$mode" "$output"
