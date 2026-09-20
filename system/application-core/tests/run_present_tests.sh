#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-sm-present-test.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
  sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -c '
  set -eu
  for checks in optimized undefined; do
  extra=""
  [ "$checks" != undefined ] || extra="-fsanitize=undefined -fno-sanitize-recover=all"
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror $extra -fPIC -shared -fvisibility=hidden \
    -I/lane/include /lane/source/gkd-sm-present.c /lane/source/gkd-app-ready.c \
    -ldl -Wl,-z,defs -o /test/libgkd-sm-present.so
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror $extra -fPIC -shared -DFIXTURE_SDL \
    /lane/tests/present_fixture.c -o /test/libfake-sdl.so
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror $extra /lane/tests/present_fixture.c \
    -L/test -Wl,-rpath,/test -lfake-sdl -o /test/present-fixture
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror $extra -I/lane/include \
    /lane/tests/test_present.c /lane/source/gkd-app-ready.c -o /test/test-present
  timeout 20 /test/test-present
  done
  '
