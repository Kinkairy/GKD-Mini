#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/targetlib"
target_p1=${GKDSU_TARGET_P1:-/opt/gkd-build/private-state/android-reverse-private/gkd-mini/current-rc35-f0c7e607-20260913/inputs/p1.img}
debugfs -R "dump /usr/lib/libcrypto.so.1.0.0 $tmp/targetlib/libcrypto.so.1.0.0" "$target_p1" >/dev/null 2>&1
debugfs -R "dump /lib/libdl-0.9.33.2.so $tmp/targetlib/libdl-0.9.33.2.so" "$target_p1" >/dev/null 2>&1
[ "$(sha256sum "$tmp/targetlib/libcrypto.so.1.0.0" | awk '{print $1}')" = 96f924f4615e9cfa59d9618684c54a723f490c1e880ea923b2dbdb8d59ec586d ]
[ "$(sha256sum "$tmp/targetlib/libdl-0.9.33.2.so" | awk '{print $1}')" = c13bef77465482686957aba16e4ccf69b31e09d38f9f8a4fe7646ff1ae411853 ]
ln -s libdl-0.9.33.2.so "$tmp/targetlib/libdl.so.0"
docker run --rm --network none -v "$lane:/lane:ro" -v "$tmp:/out" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -lc '
set -eu
flags="-std=c99 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations"
gcc $flags -I/lane/source /lane/source/gkd-update-journal.c /lane/source/gkd-update-request.c \
  /lane/source/gkd-update-prepare.c -lcrypto -o /out/gkd-update-prepare-host
gcc $flags -DGKDU_RECOVERY_HOST=1 -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/source/gkd-update-request.c /lane/source/gkd-update-prepare.c -lcrypto \
  -o /out/gkd-update-prepare-r-host
mips=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
$mips $flags -Wl,--build-id=none -I/lane/source \
  /lane/source/gkd-update-journal.c /lane/source/gkd-update-request.c /lane/source/gkd-update-prepare.c \
  -L/out/targetlib -Wl,-rpath-link,/out/targetlib \
  -l:libcrypto.so.1.0.0 -o /out/gkd-update-prepare-mips
file /out/gkd-update-prepare-mips | grep -q "ELF 32-bit LSB executable, MIPS"
/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d \
  /out/gkd-update-prepare-mips > /out/gkd-update-prepare-mips.dynamic
grep -q "libcrypto.so.1.0.0" /out/gkd-update-prepare-mips.dynamic
'
GKDSU_PREPARE_BINARY="$tmp/gkd-update-prepare-host" \
GKDSU_PREPARE_R_BINARY="$tmp/gkd-update-prepare-r-host" \
  python3 -B "$lane/tests/test_package.py"
echo 'GKDSU_PREPARE_AB=PASS host=1 mips_build=1'
