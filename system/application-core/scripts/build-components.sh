#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
out=${1:?new output directory required}
case "$out" in /tmp/gkd-mini-public/gkd-app-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/app-*) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
mkdir -m 0700 "$out"
python3 "$lane/scripts/manifest-header.py" "$lane/config/minimal-a-manifest.json" "$out/gkd-app-manifest.generated.h"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$project/system/rc33-system-update/source:/update:ro" \
  -v "$project/system/config-core/device:/config:ro" \
  -v "$project/kernel/current/initramfs:/format:ro" \
  -v "$out:/out:rw" -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -c '
  set -eu
  cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
  flags="-std=gnu99 -Os -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--build-id=none"
  $cc $flags -static -I/lane/include -I/update -I/out -I/format \
    /lane/source/gkd-app-host.c /lane/source/gkd-app-controls.c /lane/source/gkd-app-animation.c /lane/source/gkd-app-display.c /lane/source/gkd-app-profile.c /lane/source/gkd-app-diagnostics.c /lane/source/gkd-app-loop.c /lane/source/gkd-app-prepare.c \
    /lane/source/gkd-app-ready.c /lane/source/gkd-app-exec.c \
    /lane/source/gkd-app-watch.c /lane/source/gkd-app-init.c \
    /lane/source/gkd-app-release.c /lane/source/gkd-app-transfer.c \
    /update/gkd-update-sha256.c -o /out/gkd-app-host
  $cc $flags -fPIC -shared -fvisibility=hidden -I/lane/include \
    /lane/source/gkd-sm-present.c /lane/source/gkd-app-ready.c -ldl \
    -Wl,-z,defs -o /out/libgkd-sm-present.so
  /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip /out/gkd-app-host /out/libgkd-sm-present.so
  $cc $flags -static /config/gkd-config-rename.c -o /out/gkd-config-rename
  /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip /out/gkd-config-rename
  '
! readelf -l "$out/gkd-app-host" | grep -q INTERP
cp "$lane/device/gkd-application-start" "$out/gkd-application-start"
chmod 0555 "$out/gkd-application-start"
(cd "$out" && sha256sum gkd-app-host libgkd-sm-present.so gkd-application-start gkd-app-manifest.generated.h gkd-config-rename > SHA256SUMS)
