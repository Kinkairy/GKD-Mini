#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$project:/project:ro" "$image" sh -eu -c '
 python3 /project/kernel/current/tests/extract_application_power_test.py /tmp/actual-power.c
 cc -std=c99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined /tmp/actual-power.c -o /tmp/gkd-power-test
 /tmp/gkd-power-test
'
