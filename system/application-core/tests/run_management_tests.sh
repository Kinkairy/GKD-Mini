#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?new scoped output directory required}
case "$out" in /tmp/gkd-mini-public/gkd-app-management-tests-*) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
mkdir -m 0700 "$out"
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$out:/out:rw" \
  -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro \
  "$image" sh -eu -c '
management_flags="-std=gnu99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -ffunction-sections -fdata-sections"
game_flags="-std=gnu99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow"
cc $management_flags /lane/source/gkd-app-management.c -Wl,--gc-sections -o /out/gkd-app-management-host
cc $management_flags -O0 -fanalyzer -c /lane/source/gkd-app-management.c -o /out/gkd-app-management-analyzer.o 2>/out/fanalyzer.log
cc $management_flags /lane/tests/management_fixture.c \
  -Wl,--wrap=syscall -Wl,--wrap=ioctl -Wl,--gc-sections \
  -o /out/management-fixture
/out/management-fixture > /out/normal.log 2>&1
cc $game_flags /lane/tests/game_fixture.c /lane/source/gkd-app-game-control.c \
  -I/lane/include "-DGKD_APP_GAME_CLIENT=\"/usr/sbin/gkd-app-game\"" -static \
  -o /out/game-fixture
cc $management_flags -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  /lane/tests/management_fixture.c -Wl,--wrap=syscall -Wl,--wrap=ioctl \
  -o /out/management-fixture-sanitized
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 /out/management-fixture-sanitized \
  > /out/sanitized.log 2>&1
cc $game_flags -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
  /lane/tests/game_fixture.c /lane/source/gkd-app-game-control.c \
  -I/lane/include "-DGKD_APP_GAME_CLIENT=\"/usr/sbin/gkd-app-game\"" -static \
  -o /out/game-fixture-sanitized
mips=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
$mips $management_flags -Os /lane/source/gkd-app-management.c -static -Wl,--gc-sections \
  -o /out/gkd-app-management-mips
'
unshare --user --map-root-user --mount "$out/game-fixture" "$out/game-root-normal" >>"$out/normal.log" 2>&1
UBSAN_OPTIONS=halt_on_error=1 unshare --user --map-root-user --mount "$out/game-fixture-sanitized" "$out/game-root-sanitized" >>"$out/sanitized.log" 2>&1
printf '%s\n' \
  'GKD_APP_MANAGEMENT=READY' \
  'GKD_APP_MANAGEMENT_FIXTURE=PASS pid/starttime/native/comm/exe/argv/ip/listener/inode/pidfd/symlink' \
  'GKD_APP_GAME=FAILED stage=idle errno=2' \
  'GKD_APP_GAME=FAILED stage=registry errno=3' \
  'GKD_APP_GAME=FAILED stage=registry errno=71' \
  'GKD_APP_GAME=FAILED stage=namespace errno=3' \
  'GKD_APP_GAME_FIXTURE=PASS pidns/mntns/root-pin/idle/active/stale/malformed/listen-accept-reply/typed-menu-stays-active/same-menu' \
  >"$out/expected.log"
cmp "$out/expected.log" "$out/normal.log"
cmp "$out/expected.log" "$out/sanitized.log"
file "$out/gkd-app-management-mips" >"$out/mips-file.log"
if readelf -l "$out/gkd-app-management-mips" | grep -E 'INTERP|Requesting' >"$out/mips-interp.log"; then
  exit 1
fi
printf 'MIPS_INTERP=ABSENT\n' >"$out/mips-interp.log"
sha256sum \
  "$lane/source/gkd-app-management.c" "$lane/tests/management_fixture.c" \
  "$lane/tests/game_fixture.c" "$lane/source/gkd-app-game-control.c" \
  "$out/gkd-app-management-host" "$out/management-fixture" "$out/game-fixture" \
  "$out/management-fixture-sanitized" "$out/game-fixture-sanitized" \
  "$out/gkd-app-management-mips" >"$out/SHA256SUMS"
cat "$out/normal.log"
cat "$out/sanitized.log"
cat "$out/mips-file.log"
cat "$out/mips-interp.log"
cat "$out/SHA256SUMS"
printf 'GKD_APP_MANAGEMENT_TESTS=PASS normal/UBSan/fanalyzer/static-MIPS\n'