#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project=$(CDPATH= cd -- "$lane/../.." && pwd)
source_p1=${1:?source P1 required}
runtime_id=${2:?64-hex runtime id required}
output=${3:?new output directory required}
profile=${4:-normal}
case "$profile" in normal) recovery_flags= ;; dedicated-r) recovery_flags=-DGKDU_RECOVERY_HOST=1 ;; *) exit 2 ;; esac
case "$runtime_id" in *[!0-9a-f]*|'') exit 2;; esac
[ "${#runtime_id}" -eq 64 ] && [ -f "$source_p1" ] && [ ! -e "$output" ]
mkdir -m 0700 "$output"
tmp=$(mktemp -d /tmp/gkd-mini-public/gkdsu-components.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/targetlib"
debugfs -R "dump /usr/lib/libcrypto.so.1.0.0 $tmp/targetlib/libcrypto.so.1.0.0" "$source_p1" >/dev/null 2>&1
debugfs -R "dump /lib/libdl-0.9.33.2.so $tmp/targetlib/libdl-0.9.33.2.so" "$source_p1" >/dev/null 2>&1
[ "$(sha256sum "$tmp/targetlib/libcrypto.so.1.0.0" | awk '{print $1}')" = 96f924f4615e9cfa59d9618684c54a723f490c1e880ea923b2dbdb8d59ec586d ]
[ "$(sha256sum "$tmp/targetlib/libdl-0.9.33.2.so" | awk '{print $1}')" = c13bef77465482686957aba16e4ccf69b31e09d38f9f8a4fe7646ff1ae411853 ]
ln -s libdl-0.9.33.2.so "$tmp/targetlib/libdl.so.0"
docker run --rm --network none -e "GKDU_RECOVERY_FLAGS=$recovery_flags" -v "$lane:/lane:ro" -v "$tmp:/out" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  local/c-builder:2026.08.02-kernel sh -lc '
set -eu
cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
flags="-std=c99 -Os -Wall -Wextra -Werror -Wno-deprecated-declarations -Wl,--build-id=none"
crypto="-L/out/targetlib -Wl,-rpath-link,/out/targetlib -l:libcrypto.so.1.0.0"
$cc $flags -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/source/gkd-update-sha256.c /lane/source/gkd-update-engine.c \
  -o /out/gkd-update-engine
$cc $flags $GKDU_RECOVERY_FLAGS -I/lane/source /lane/source/gkd-update-journal.c /lane/source/gkd-update-request.c \
  /lane/source/gkd-update-prepare.c $crypto -o /out/gkd-update-prepare
$cc $flags $GKDU_RECOVERY_FLAGS -I/lane/source /lane/source/gkd-update-journal.c \
  /lane/source/gkd-update-request.c /lane/source/gkd-update-sha256.c \
  /lane/source/gkd-update-coordinator.c -o /out/gkd-update-coordinator
static_flags="-static -flto -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -Wl,--gc-sections -Wl,-z,norelro"
$cc $flags $static_flags -I/lane/source -Dmain=gkdu_engine_main \
  -c /lane/source/gkd-update-engine.c -o /out/engine.o
$cc $flags $static_flags $GKDU_RECOVERY_FLAGS -I/lane/source -Dmain=gkdu_coordinator_main \
  -c /lane/source/gkd-update-coordinator.c -o /out/coordinator.o
for source in gkd-update-journal gkd-update-request gkd-update-sha256 gkd-update-runtime; do
  $cc $flags $static_flags -I/lane/source -c /lane/source/$source.c -o /out/$source.o
done
$cc $flags $static_flags /out/engine.o /out/coordinator.o \
  /out/gkd-update-journal.o /out/gkd-update-request.o /out/gkd-update-sha256.o \
  /out/gkd-update-runtime.o -o /out/gkd-update-runtime-static
$cc $flags -I/lane/source /lane/source/gkd-update-request.c \
  /lane/source/gkd-update-request-tool.c -o /out/gkd-update-request-tool
strip=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip
$strip /out/gkd-update-engine /out/gkd-update-prepare \
  /out/gkd-update-coordinator /out/gkd-update-request-tool \
  /out/gkd-update-runtime-static
objcopy=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-objcopy
$objcopy --remove-section=.eh_frame --remove-section=.comment \
  /out/gkd-update-runtime-static
/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d /out/gkd-update-prepare | grep -q libcrypto.so.1.0.0
/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d /out/targetlib/libcrypto.so.1.0.0 | grep -q libdl.so.0
for f in /out/gkd-update-engine /out/gkd-update-coordinator; do
  ! /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -d "$f" | grep -q libcrypto
done
for f in /out/gkd-update-runtime-static; do
  ! /opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-readelf -l "$f" | grep -q INTERP
done
'
for name in gkd-update-engine gkd-update-prepare gkd-update-coordinator gkd-update-request-tool \
  gkd-update-runtime-static; do
	cp "$tmp/$name" "$output/$name"; chmod 0555 "$output/$name"
done
cp "$tmp/targetlib/libcrypto.so.1.0.0" "$output/libcrypto.so.1.0.0"
cp "$tmp/targetlib/libdl-0.9.33.2.so" "$output/libdl-0.9.33.2.so"
chmod 0444 "$output/libcrypto.so.1.0.0"
chmod 0555 "$output/libdl-0.9.33.2.so"
cp "$lane/source/gkd-system-update" "$output/gkd-system-update"
cp "$lane/source/S88gkd-update-trial" "$output/S88gkd-update-trial"
chmod 0555 "$output/gkd-system-update" "$output/S88gkd-update-trial"
printf '%s\n' "$runtime_id" >"$output/runtime-id"; chmod 0444 "$output/runtime-id"
python3 "$lane/tools/xml_trust_to_pem.py" \
  "$project/system/gkd-card-writer/trust-anchor.xml" "$output/update-public.pem"
chmod 0444 "$output/update-public.pem"
(cd "$output" && sha256sum * >SHA256SUMS)
echo 'GKDSU_COMPONENTS=PASS'
