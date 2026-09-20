#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
temporary=$(mktemp -d /tmp/gkd-mini-public/gkd-ui-boot-test.XXXXXX)
trap 'rm -rf -- "$temporary"' EXIT HUP INT TERM
awk -F '|' '!/^#/ && NF == 8 {print $1 "=" $3}' \
  "$project/system/config-core/schema/gdkmini.schema" >"$temporary/effective.conf"
python3 "$lane/scripts/generate-boot-art.py" --source "$lane/assets/recovery-boot.png" \
  --config "$temporary/effective.conf" --output "$temporary/gkd-boot-art.generated.h"
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -v "$project:/project:ro" -v "$temporary:/test:rw" \
  local/c-builder:2026.08.02-kernel \
  python3 -B /project/system/ui-core/tests/test_boot_frame.py /test
