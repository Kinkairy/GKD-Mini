#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
for flags in '-O2' '-O1 -fsanitize=undefined -fno-sanitize-recover=all'; do
 docker run --rm --network none --cap-add SYS_ADMIN --security-opt seccomp=unconfined \
  -v "$project:/src:ro" "$image" sh -c '
  cc -std=gnu99 -Wall -Wextra -Werror '"$flags"' -I/src/system/application-core/include \
   /src/system/application-core/tests/storage_fixture.c -o /tmp/storage-test
  /tmp/storage-test'
done
