#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
[ "$#" -le 1 ] || { echo 'usage: run_profile_tests.sh [evidence-json]' >&2; exit 64; }
evidence=${GKD_PROFILE_EVIDENCE:-${1:-}}
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-profile-mount-test.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$lane:/lane:ro" -v "$temporary:/test:rw" "$image" sh -c '
  set -eu
  common="-std=gnu99 -O2 -Wall -Wextra -Werror -I/lane/include"
  gcc $common /lane/tests/profile_mount_fixture.c /lane/source/gkd-app-profile.c -o /test/profile-mount
  gcc $common -DREFUSE_FILE_BIND -Wl,--wrap=mount /lane/tests/profile_mount_fixture.c /lane/source/gkd-app-profile.c -o /test/profile-mount-refuse
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror -fPIC -shared /lane/tests/profile_preload_fixture.c -o /test/libgkd-sm-present.so
  gcc -std=gnu99 -O2 -Wall -Wextra -Werror /lane/tests/profile_target_fixture.c -o /test/profile-target
  '
if [ -n "$evidence" ]; then
  python3 "$lane/tests/test_profile_mount.py" --fixture "$temporary/profile-mount" \
    --refuse-fixture "$temporary/profile-mount-refuse" --library "$temporary/libgkd-sm-present.so" \
    --target "$temporary/profile-target" --evidence "$evidence"
else
  python3 "$lane/tests/test_profile_mount.py" --fixture "$temporary/profile-mount" \
    --refuse-fixture "$temporary/profile-mount-refuse" --library "$temporary/libgkd-sm-present.so" \
    --target "$temporary/profile-target"
fi
