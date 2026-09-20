#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-opk-plan-test.XXXXXX)
case "$(realpath -- "$temporary")" in /tmp/gkd-mini-public/gkd-opk-plan-test.*) ;; *) exit 2 ;; esac
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1" ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -eu -c '
flags="-std=c11 -O2 -Wall -Wextra -Werror -pedantic -I/lane/include -I/lane/third_party/libopk-5cb5230 -DGKD_OPK_PLAN_TESTING"
cc $flags /lane/source/gkd-opk-plan.c /lane/tests/test_gkd_opk_plan.c -o /test/normal
/test/normal
cc $flags -g -fsanitize=address -fno-omit-frame-pointer /lane/source/gkd-opk-plan.c /lane/tests/test_gkd_opk_plan.c -o /test/asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 /test/asan
cc $flags -g -fsanitize=undefined -fno-omit-frame-pointer /lane/source/gkd-opk-plan.c /lane/tests/test_gkd_opk_plan.c -o /test/ubsan
UBSAN_OPTIONS=halt_on_error=1 /test/ubsan
'