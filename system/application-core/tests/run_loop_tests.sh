#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-app-loop-test.XXXXXX)
case "$(realpath -- "$temporary")" in /tmp/gkd-mini-public/gkd-app-loop-test.*) ;; *) exit 2 ;; esac
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -c '
  set -eu
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_loop.c /lane/source/gkd-app-loop.c -o /test/test-loop \
    -Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=fcntl -Wl,--wrap=ioctl -Wl,--wrap=mount -Wl,--wrap=close
  ASAN_OPTIONS=detect_leaks=1 timeout 15 /test/test-loop
  '
