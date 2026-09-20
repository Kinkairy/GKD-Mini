#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
out=${1:-/tmp/gkd-mini-public/gkd-service-core-build}
toolroot=/opt/gkd-build/private-state/android-reverse-private/gkd-mini/toolchains/rc282-exact-gcw0-d28-20260816
image=local/c-builder:2026.08.02-kernel

case "$out" in /tmp/gkd-mini-public/gkd-service-core-*) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
mkdir -m 0700 "$out"
cp "$lane/source/gkd-input-idle.c" "$out/"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$out:/work:rw" -v "$toolroot:/opt/toolchain:ro" "$image" sh -ec '
    cc -std=gnu99 -Wall -Wextra -Werror /work/gkd-input-idle.c -o /tmp/gkd-input-idle-test
    /tmp/gkd-input-idle-test --self-test
    mcc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
    strip=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip
    "$mcc" -std=gnu99 -Os -Wall -Wextra -Werror -Wl,--build-id=none \
      /work/gkd-input-idle.c -o /work/gkd-input-idle.unstripped
    "$strip" -o /work/gkd-input-idle /work/gkd-input-idle.unstripped
    rm /work/gkd-input-idle.unstripped
  '
file "$out/gkd-input-idle" | grep -F 'ELF 32-bit LSB executable, MIPS' >/dev/null
readelf -l "$out/gkd-input-idle" | grep -F '/lib/ld-uClibc.so.0' >/dev/null
(cd "$out" && sha256sum gkd-input-idle.c gkd-input-idle >SHA256SUMS)
printf 'GKD_SERVICE_CORE_BUILD=PASS output=%s\n' "$out"
