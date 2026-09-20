#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-app-ready-test.XXXXXX)
# mktemp guarantees this exact task-local target; no caller-provided deletion.
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
  sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" \
  sh -c 'gcc -std=gnu99 -O2 -Wall -Wextra -Werror -I/lane/include \
    /lane/source/gkd-app-ready.c /lane/tests/test_ready.c -o /test/ready && \
    /test/ready && \
    gcc -std=gnu99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I/lane/include /lane/source/gkd-app-ready.c \
    /lane/tests/test_ready.c -o /test/ready-sanitize && /test/ready-sanitize'
