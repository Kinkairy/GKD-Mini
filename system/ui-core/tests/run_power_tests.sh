#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$project:/project:ro" local/c-builder:2026.08.02-kernel \
  python3 /project/system/ui-core/tests/test_r_poweroff.py -q
