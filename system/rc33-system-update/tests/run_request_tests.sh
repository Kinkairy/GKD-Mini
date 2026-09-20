#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
docker run --rm --network none -v "$lane:/lane:ro" -v "$tmp:/out" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -lc '
set -eu
flags="-std=c99 -O2 -Wall -Wextra -Werror"
gcc $flags -I/lane/source /lane/source/gkd-update-request.c \
  /lane/tests/test_request.c -o /out/gkd-update-request-host
mips=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
$mips $flags -static -Wl,--build-id=none -I/lane/source \
  /lane/source/gkd-update-request.c /lane/tests/test_request.c \
  -o /out/gkd-update-request-mips
file /out/gkd-update-request-mips | grep -q "ELF 32-bit LSB executable, MIPS"
/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d \
  /out/gkd-update-request-mips 2>&1 | grep -q "There is no dynamic section"
'
"$tmp/gkd-update-request-host" "$tmp/request.img"
echo 'GKDSU_REQUEST_AB=PASS host=1 mips_static=1'
