#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$project:/project:ro" local/c-builder:2026.08.02-kernel sh -eu -c '
python3 /project/kernel/current/tests/extract_dma_compositor_test.py /tmp/gkd-dma.c
cc -std=gnu99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined /tmp/gkd-dma.c -o /tmp/gkd-dma
/tmp/gkd-dma
'
