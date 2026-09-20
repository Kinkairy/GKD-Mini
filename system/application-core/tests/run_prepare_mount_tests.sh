#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-app-prepare-mount.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = \
 sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
for variant in normal ubsan; do
 extra=
 [ "$variant" != ubsan ] || extra="-fsanitize=undefined -fno-sanitize-recover=all"
 docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" \
  gcc -static -std=gnu99 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  $extra -I/lane/include /lane/tests/prepare_mount_fixture.c \
  -Wl,--gc-sections,--wrap=fstatfs -o "/test/fixture-$variant"
 mkdir "$temporary/$variant"
 timeout 15 unshare --user --map-root-user --mount --pid --fork \
  "$temporary/fixture-$variant" "$temporary/$variant"
done
