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
gcc $flags -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/tests/test_journal.c -o /out/test-journal-host
/out/test-journal-host /out/recovery-host.img
mips=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
$mips $flags -static -Wl,--build-id=none -I/lane/source \
  /lane/source/gkd-update-journal.c /lane/tests/test_journal.c \
  -o /out/test-journal-mips
file /out/test-journal-mips | grep -q "ELF 32-bit LSB executable, MIPS"
'
if [ -n "${GKD_MIPS_OUTPUT:-}" ]; then
  test ! -e "$GKD_MIPS_OUTPUT"
  cp "$tmp/test-journal-mips" "$GKD_MIPS_OUTPUT"
  chmod 0555 "$GKD_MIPS_OUTPUT"
fi
echo 'GKDSU_JOURNAL_AB=PASS host=1 mips_build=1'
