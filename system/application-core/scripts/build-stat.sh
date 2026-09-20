#!/bin/sh
# Build the accepted BusyBox stat leaf, absent from the embedded shared BusyBox.
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?new output directory required}
archive=${2:?pinned BusyBox 1.22.1 archive required}
mode=${3:-mips}
case "$mode" in host|mips) ;; *) exit 2;; esac
case "$out" in /tmp/gkd-mini-public/gkd-app-stat-*) ;; *) exit 2;; esac
[ ! -e "$out" ]
[ "$(sha256sum "$archive" | cut -d ' ' -f 1)" = ae0b029d0a9e4dd71a077a790840e496dd838998e4571b87b60fed7462b6678b ]
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
mkdir -m 0700 "$out"
docker run --rm --network none --user "$(id -u):$(id -g)" \
 -v "$lane:/lane:ro" -v "$archive:/source.tar.bz2:ro" -v "$out:/out:rw" \
 -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro "$image" sh -ec '
 tar --no-same-owner -xjf /source.tar.bz2 -C /out
 cd /out/busybox-1.22.1
 export PATH=/opt/toolchain/host/bin:$PATH
 KCONFIG_ALLCONFIG=/lane/config/stat.config make allnoconfig >/out/config.log 2>&1
 cross=;strip=strip
 if [ "$1" = mips ]; then cross=mipsel-gcw0-linux-uclibc-;strip=mipsel-gcw0-linux-uclibc-strip;fi
 make -j2 CROSS_COMPILE="$cross" >/out/build.log 2>&1
 cp busybox /out/stat
 "$strip" /out/stat
 if [ "$1" = host ]; then
  touch /out/metadata-fixture;chmod 0600 /out/metadata-fixture
  [ "$(/out/stat -c %u:%a /out/metadata-fixture)" = "$(id -u):600" ]
 fi
' sh "$mode"
! readelf -l "$out/stat" | grep -q INTERP
(cd "$out" && sha256sum stat >SHA256SUMS)
printf 'GKD_APP_STAT_BUILD=PASS mode=%s\n' "$mode"
