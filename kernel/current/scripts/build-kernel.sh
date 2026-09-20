#!/bin/sh
set -eu

project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
output=${1:?usage: build-kernel.sh OUTPUT [INIT_SOURCE] [RECOVERY_HEADER]}
init_source=${2:-$project/kernel/current/initramfs/round83-supervisor.c}
recovery_header=${3:-}
image=local/c-builder:2026.08.02-kernel
image_id=sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1
source_root=/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1
toolchain=/opt/gkd-build/.cache/gkd-mini-v1.2/ingenic-toolchain-v5.2

case "$output" in
  /tmp/gkd-mini-public/gkd-kernel-current-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/kernel/current-*) ;;
  *) echo GKD_CURRENT_KERNEL=BLOCKED unsafe-output >&2; exit 2 ;;
esac
case "$init_source" in
  "$project"/kernel/current/initramfs/*.c)
    container_init=/workspace/${init_source#"$project"/}
    ;;
  /tmp/gkd-mini-public/gkd-current-init-*/*.c)
    container_init=$init_source
    ;;
  *) echo GKD_CURRENT_KERNEL=BLOCKED unsafe-init-source >&2; exit 2 ;;
esac
[ ! -e "$output" ] && [ -f "$init_source" ] && [ ! -L "$init_source" ]
if [ -n "$recovery_header" ]; then
  case "$recovery_header" in
    /tmp/gkd-mini-public/gkd-current-init-*/*) ;;
    *) echo GKD_CURRENT_KERNEL=BLOCKED unsafe-recovery-header >&2; exit 2 ;;
  esac
  [ -f "$recovery_header" ] && [ ! -L "$recovery_header" ]
fi
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "$image_id" ]

docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e HOME=/opt/gkd-build -e GKD_CURRENT_INIT_SOURCE="$container_init" \
  -e GKD_CURRENT_BUILD_MODE="${GKD_CURRENT_BUILD_MODE:-normal}" \
  -e GKD_RECOVERY_CAPSULE_HEADER="$recovery_header" \
  -v "$project:/workspace:ro" \
  -v "$source_root:$source_root:rw" \
  -v "$toolchain:$toolchain:ro" \
  -v /tmp/gkd-mini-public:/tmp/gkd-mini-public:rw \
  -v "${output%/*}:${output%/*}:rw" \
  "$image" sh /workspace/kernel/current/scripts/build-in-container.sh "$output"
