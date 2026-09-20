#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-app-exec-test.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
  sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -c '
  set -eu
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden \
    -I/lane/include /lane/source/gkd-sm-present.c /lane/source/gkd-app-ready.c \
    -ldl -Wl,-z,defs -o /test/libgkd-sm-present.so
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fPIC -shared -DFIXTURE_SDL \
    /lane/tests/present_fixture.c -o /test/libfake-sdl.so
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror /lane/tests/exec_fixture.c \
    -L/test -Wl,-rpath,/test -lfake-sdl -o /test/exec-fixture
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_exec.c /lane/source/gkd-app-exec.c \
    /lane/source/gkd-app-ready.c -o /test/test-exec
  timeout 20 /test/test-exec
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_exec_report.c /lane/source/gkd-app-exec.c \
    /lane/source/gkd-app-ready.c -o /test/test-exec-report
  timeout 20 /test/test-exec-report
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_watch.c /lane/source/gkd-app-watch.c \
    /lane/source/gkd-app-exec.c /lane/source/gkd-app-ready.c -o /test/test-watch
  timeout 20 /test/test-watch
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_watch_random.c /lane/source/gkd-app-watch.c \
    /lane/source/gkd-app-exec.c /lane/source/gkd-app-ready.c \
    -Wl,--wrap=syscall -o /test/test-watch-random
  timeout 5 /test/test-watch-random
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_release.c /lane/source/gkd-app-release.c \
    /lane/source/gkd-app-init.c /lane/source/gkd-app-watch.c /lane/source/gkd-app-exec.c \
    /lane/source/gkd-app-ready.c -o /test/test-release
  timeout 20 /test/test-release
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_transfer.c /lane/source/gkd-app-transfer.c \
    /lane/source/gkd-app-watch.c /lane/source/gkd-app-exec.c /lane/source/gkd-app-ready.c \
    -o /test/test-transfer
  timeout 20 /test/test-transfer
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_prepare.c /lane/source/gkd-app-prepare.c \
    /lane/source/gkd-app-release.c /lane/source/gkd-app-init.c /lane/source/gkd-app-transfer.c \
    /lane/source/gkd-app-watch.c /lane/source/gkd-app-exec.c /lane/source/gkd-app-ready.c \
    -o /test/test-prepare
  timeout 5 /test/test-prepare
  '
