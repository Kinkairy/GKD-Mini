#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-app-animation-test.XXXXXX)
case "$(realpath -- "$temporary")" in /tmp/gkd-mini-public/gkd-app-animation-test.*) ;; *) exit 2 ;; esac

image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$project:/project:ro" -v "$temporary:/test:rw" "$image" sh -c '
set -eu
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=undefined,address -fno-sanitize-recover=all -I/project/system/application-core/include -I/project/kernel/current/initramfs /project/system/application-core/tests/animation_fixture.c /project/system/application-core/source/gkd-app-animation.c -o /test/animation -Wl,--wrap=open,--wrap=ioctl,--wrap=waitpid,--wrap=clock_gettime
ASAN_OPTIONS=detect_leaks=1 timeout 20 /test/animation
'

printf 'GKD_ANIMATION_TEST_EVIDENCE=%s\n' "$temporary"
