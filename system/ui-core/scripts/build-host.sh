#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
output=${1:?output directory required}
profile=${2:-normal}
image=local/c-builder:2026.08.02-kernel
image_id=sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1
font_source=/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1/lib/fonts/font_8x16.c

case "$output" in
  /tmp/gkd-mini-public/gkd-ui-*) ;;
  *) echo GKD_UI_HOST_BUILD=BLOCKED unsafe-output >&2; exit 2 ;;
esac
[ ! -e "$output" ] && [ -f "$font_source" ] && [ ! -L "$font_source" ]
case "$profile" in normal|dedicated-r) ;; *) echo GKD_UI_HOST_BUILD=BLOCKED invalid-profile >&2; exit 2 ;; esac
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "$image_id" ]
parent=${output%/*}
name=${output##*/}
mkdir -p "$parent"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$parent:/out:rw" \
  -v "$lane/../config-core:/config-core:ro" \
  -v "$lane/../service-core:/service-core:ro" \
  -v "$font_source:/font_8x16.c:ro" \
  "$image" /lane/scripts/build.sh "/out/$name" /font_8x16.c host "$profile"
file "$output/gkd-recovery-ui" | grep -q 'ELF 64-bit LSB.*x86-64'
if [ "$profile" = dedicated-r ]; then
  [ -x "$output/gkd-input" ] && [ -x "$output/gkd-screenshot" ]
  file "$output/gkd-input" | grep -q 'ELF 64-bit LSB.*x86-64'
  file "$output/gkd-screenshot" | grep -q 'ELF 64-bit LSB.*x86-64'
fi
printf 'GKD_UI_HOST_BUILD=PASS output=%s\n' "$output"
