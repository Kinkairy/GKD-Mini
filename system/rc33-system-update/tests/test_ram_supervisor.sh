#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project=$(CDPATH= cd -- "$lane/../.." && pwd)
p1=${GKDSU_TARGET_P1:-/opt/gkd-build/private-state/android-reverse-private/gkd-mini/current-rc35-f0c7e607-20260913/inputs/p1.img}
p1_sha=${GKDSU_TARGET_P1_SHA256:-11227ffdf3125b7961c3aea50203da5eaf423c11186e40b39c426cdbf52f3cac}
tmp=$(mktemp -d /tmp/gkd-mini-public/gkdsu-supervisor.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
components=${GKDSU_COMPONENTS:-$tmp/components}
if [ -z "${GKDSU_COMPONENTS:-}" ]; then
  mkdir "$components"
  printf 'gkd-update-runtime-static-fixture\n' >"$components/gkd-update-runtime-static"
fi
python3 "$lane/scripts/generate-ram-runtime.py" --p1 "$p1" \
  --p1-sha256 "$p1_sha" --components "$components" \
  --output "$tmp/round84-update-runtime.generated.h"
python3 "$lane/scripts/derive-update-supervisor.py" \
  --source "$project/kernel/current/initramfs/round83-supervisor.c" \
  --output "$tmp/round84-supervisor.generated.c"
cp "$project/kernel/current/initramfs/round83-maintenance-loader.generated.h" \
  "$project/kernel/current/initramfs/round83-ram-payload.generated.h" \
  "$project/kernel/current/initramfs/round64-request-gate.h" \
  "$project/kernel/current/initramfs/round38-animation-format.h" "$tmp/"
grep -Fq 'r64m_mount_readonly_wait(R64M_P1_DEVICE' \
  "$tmp/round83-maintenance-loader.generated.h"
grep -Fq 'result != -16' "$tmp/round83-maintenance-loader.generated.h"
grep -Fq 'R64M_READONLY_MOUNT_STEPS 50U' \
  "$tmp/round83-maintenance-loader.generated.h"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$tmp:/out:rw" \
  -v /opt/gkd-build/private-state/gkd-mini-system-rebuild:/opt/gkd-build/private-state/gkd-mini-system-rebuild:ro \
  -v /opt/gkd-build/.cache/gkd-mini-v1.2:/opt/gkd-build/.cache/gkd-mini-v1.2:ro \
  local/c-builder:2026.08.02-kernel sh -ec '
cc=/opt/gkd-build/.cache/gkd-mini-v1.2/ingenic-toolchain-v5.2/toolchain/bin/mips-linux-gnu-gcc
nolibc=/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1/tools/include/nolibc
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -Wno-unused-function \
  -Wno-unused-parameter -Wno-unused-variable -fno-asynchronous-unwind-tables \
  -fno-ident -fno-stack-protector -ffunction-sections -fdata-sections \
  -nostdlib -static -Wl,--build-id=none -Wl,--gc-sections \
  -I"$nolibc" -I/out /out/round84-supervisor.generated.c -lgcc \
  -o /out/round84-supervisor.mips
'
file "$tmp/round84-supervisor.mips" | grep -q 'ELF 32-bit LSB executable, MIPS'
echo "GKDSU_RAM_SUPERVISOR=PASS bytes=$(wc -c <"$tmp/round84-supervisor.mips")"
