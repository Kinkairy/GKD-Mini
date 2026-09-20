#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-ui-host-test.XXXXXX)
output=$temporary/gkd-ui-build
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
"$lane/scripts/build-host.sh" "$output" dedicated-r
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e GKD_UI_TEST_BINARY=/ui/gkd-recovery-ui \
  -e GKD_UI_TEST_FONT=/ui/fallback.psf \
  -e GKD_UI_PRINT_HASHES \
  -v "$project:/project:ro" -v "$output:/ui:ro" \
  local/c-builder:2026.08.02-kernel \
  python3 /project/system/ui-core/tests/test_ui_core.py -q
