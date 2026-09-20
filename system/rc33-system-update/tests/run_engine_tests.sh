#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
docker run --rm --network none -v "$lane:/lane:ro" -v "$tmp:/out" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -lc '
set -eu
defines="-DGKDU_ENABLE_FAULT_INJECTION -DGKDU_P1_BYTES=131072ULL -DGKDU_KERNEL_BYTES=16384ULL -DGKDU_KERNEL_SLOT_OFFSET=8192ULL -DGKDU_RECOVERY_BYTES=524288ULL -DGKDU_KERNEL_BACKUP_OFFSET=16384u -DGKDU_KERNEL_BACKUP_BYTES=16384u -DGKDU_P1_BACKUP_OFFSET=65536u"
flags="-std=c99 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations"
gcc $flags $defines -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/source/gkd-update-sha256.c /lane/source/gkd-update-engine.c \
  -o /out/gkd-update-engine-host
mips=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
$mips $flags $defines -Wl,--build-id=none -I/lane/source \
  /lane/source/gkd-update-journal.c /lane/source/gkd-update-sha256.c \
  /lane/source/gkd-update-engine.c -o /out/gkd-update-engine-mips
file /out/gkd-update-engine-mips | grep -q "ELF 32-bit LSB executable, MIPS"
/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d \
  /out/gkd-update-engine-mips > /out/gkd-update-engine-mips.dynamic
! grep -q "libcrypto" /out/gkd-update-engine-mips.dynamic
! grep -q "libz.so.1" /out/gkd-update-engine-mips.dynamic
'
python3 -B "$lane/tests/test_engine.py" "$tmp/gkd-update-engine-host"
if [ -n "${GKD_MIPS_OUTPUT:-}" ]; then
  test ! -e "$GKD_MIPS_OUTPUT"
  cp "$tmp/gkd-update-engine-mips" "$GKD_MIPS_OUTPUT"
  chmod 0555 "$GKD_MIPS_OUTPUT"
fi
echo 'GKDSU_ENGINE_AB=PASS host_faults=1 mips_build=1'
