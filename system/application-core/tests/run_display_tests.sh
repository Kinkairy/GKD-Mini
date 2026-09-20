#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=$(mktemp -d /tmp/gkd-mini-public/gkd-app-display-test.XXXXXX)
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$out:/out:rw" "$image" sh -c '
set -eu
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I/lane/include /lane/source/gkd-app-display.c /lane/tests/display_fixture.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=clock_gettime,--wrap=poll -o /out/test
ASAN_OPTIONS=detect_leaks=1 timeout 10 /out/test
'
printf 'GKD_DISPLAY_TEST_EVIDENCE=%s
' "$out"
