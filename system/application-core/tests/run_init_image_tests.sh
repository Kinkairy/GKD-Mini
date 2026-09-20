#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-init-image-test.XXXXXX)
case "$(realpath -- "$temporary")" in /tmp/gkd-mini-public/gkd-init-image-test.*) ;; *) exit 2 ;; esac
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
  sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -c '
  set -eu
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror /lane/tests/init_image_fixture.c -o /test/init-image-fixture
  cp /test/init-image-fixture /test/init-image-unlinked
  cp /test/init-image-fixture /test/init-image-copy
  touch /test/plain
  chmod 644 /test/plain
  ln -s /test/init-image-fixture /test/init-image-link
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I/lane/include /lane/tests/test_init_image.c /lane/source/gkd-app-init.c -o /test/test-init-image
  timeout 15 /test/test-init-image
  '
