#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?new scoped output required}
case "$out" in /tmp/gkd-mini-public/gkd-app-controls-tests-*) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
mkdir -m 700 "$out"
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$out:/out:rw" "$image" sh -eu -c '
  for mode in native sanitized; do
    flags="-std=c99 -O1 -g -Wall -Wextra -Werror -Wconversion -Wshadow"
    [ "$mode" != sanitized ] || flags="$flags -fsanitize=address,undefined"
    cc $flags -I/lane/include /lane/source/gkd-app-controls.c /lane/tests/controls_lifecycle_fixture.c -o /out/$mode
    /out/$mode
  done
'
