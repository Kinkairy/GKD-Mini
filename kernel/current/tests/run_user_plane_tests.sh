#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$project:/project:ro"  local/c-builder:2026.08.02-kernel sh -eu -c '
 python3 /project/kernel/current/tests/extract_user_plane_test.py /tmp/actual-plane.c
 cc -std=c99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined  -I/project/system/ui-core/include /tmp/actual-plane.c -o /tmp/gkd-user-plane-test
 /tmp/gkd-user-plane-test
 '
