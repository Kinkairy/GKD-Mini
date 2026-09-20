#!/bin/sh
# Offline input only. Output is native test evidence, NOT a firmware component.
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
[ "$#" = 2 ] || { echo 'usage: build_init_probe.sh upstream-tar.bz2 empty-output-directory' >&2; exit 2; }
archive=$(realpath -- "$1")
output=$(realpath -- "$2")
case "$output" in /tmp/gkd-mini-public/*) ;; *) echo 'output must be NUC temporary storage' >&2; exit 2 ;; esac
[ -d "$output" ] && [ -z "$(ls -A "$output")" ]
[ "$(sha256sum "$archive" | cut -d ' ' -f 1)" = ae0b029d0a9e4dd71a077a790840e496dd838998e4571b87b60fed7462b6678b ]
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
  sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$archive:/source.tar.bz2:ro" -v "$output:/test:rw" \
  "$image" sh -c '
    set -eu
    tar -xjf /source.tar.bz2 -C /test
    cd /test/busybox-1.22.1
    KCONFIG_ALLCONFIG=/lane/tests/init-native.config make allnoconfig > /test/config.log 2>&1
    make -j2 > /test/build.log 2>&1
    cp busybox /test/busybox-init-native
    gcc -std=gnu99 -static -O2 -Wall -Wextra -Werror -I/lane/include \
      /lane/tests/init_fixture.c /lane/source/gkd-app-ready.c -o /test/init-fixture
    gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fPIC -shared -I/lane/include \
      /lane/source/gkd-app-ready.c /lane/source/gkd-app-exec.c \
      /lane/source/gkd-app-watch.c /lane/source/gkd-app-init.c /lane/source/gkd-app-release.c \
      /lane/source/gkd-app-transfer.c /lane/source/gkd-app-prepare.c \
      -o /test/libgkd-app-ready-native.so
    sha256sum /test/busybox-init-native /test/init-fixture
  '
