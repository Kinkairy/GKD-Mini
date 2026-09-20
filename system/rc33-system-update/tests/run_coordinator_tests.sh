#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
docker run --rm --network none -v "$lane:/lane:ro" -v "$tmp:/out" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -lc '
set -eu
flags="-std=c99 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations"
defines="-DGKDU_P1_BYTES=131072ULL -DGKDU_KERNEL_BYTES=16384ULL -DGKDU_KERNEL_SLOT_OFFSET=8192ULL -DGKDU_RECOVERY_BYTES=524288ULL -DGKDU_KERNEL_BACKUP_OFFSET=16384u -DGKDU_KERNEL_BACKUP_BYTES=16384u -DGKDU_P1_BACKUP_OFFSET=65536u"
gcc $flags $defines -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/source/gkd-update-sha256.c /lane/source/gkd-update-engine.c \
  -lz -o /out/gkd-update-engine-host
for cc in gcc /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real; do
  suffix=host; [ "$cc" = gcc ] || suffix=mips
  $cc $flags $defines -Wl,--build-id=none -I/lane/source \
    /lane/source/gkd-update-journal.c /lane/source/gkd-update-request.c \
    /lane/source/gkd-update-sha256.c /lane/source/gkd-update-coordinator.c \
    -o /out/gkd-update-coordinator-$suffix
  $cc $flags -Wl,--build-id=none -I/lane/source \
    /lane/source/gkd-update-request.c /lane/source/gkd-update-request-tool.c \
    -o /out/gkd-update-request-tool-$suffix
  $cc $flags $defines -DGKDU_RECOVERY_HOST=1 -Wl,--build-id=none -I/lane/source \
    /lane/source/gkd-update-journal.c /lane/source/gkd-update-request.c \
    /lane/source/gkd-update-sha256.c /lane/source/gkd-update-coordinator.c \
    -o /out/gkd-update-coordinator-r-$suffix
done
file /out/gkd-update-coordinator-mips | grep -q "ELF 32-bit LSB executable, MIPS"
file /out/gkd-update-request-tool-mips | grep -q "ELF 32-bit LSB executable, MIPS"
! /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d \
  /out/gkd-update-coordinator-mips | grep -q "libcrypto"
'
python3 -B "$lane/tests/test_coordinator.py" \
  "$tmp/gkd-update-coordinator-host" "$tmp/gkd-update-engine-host" \
  "$tmp/gkd-update-coordinator-r-host"
sh -n "$lane/source/gkd-system-update"
sh -n "$lane/source/S88gkd-update-trial"
echo 'GKDSU_COORDINATOR_AB=PASS host=1 mips_build=1 scripts=2'
