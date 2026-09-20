#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
source_p1=${1:?accepted source P1 required}
p1_sha=${2:?accepted source P1 sha256 required}
runtime_id=${3:?accepted runtime id required}
source_slot=${4:?source slot required}
output=${5:?output directory required}

case "$output" in
  /tmp/gkd-mini-public/gkd-ar-dedicated-r-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/ar-dedicated-r-*) ;;
  *) echo GKD_AR_R=BLOCKED unsafe-output >&2; exit 2 ;;
esac
[ ! -e "$output" ] && [ -f "$source_p1" ] && [ ! -L "$source_p1" ]
[ "$(sha256sum "$source_p1" | awk '{print $1}')" = "$p1_sha" ]
case "$runtime_id" in ''|*[!0-9a-f]*) exit 2 ;; esac
[ "${#runtime_id}" -eq 64 ]
mkdir -p "$output"
kernel_output=${output%/*}/gkd-kernel-current-${output##*/}
components=${output%/*}/gkd-components-${output##*/}
[ ! -e "$kernel_output" ] && [ ! -e "$components" ]
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  if [ "$status" -eq 0 ]; then
    rm -rf -- "$kernel_output" "$components"
  else
    printf 'GKD_AR_R=BLOCKED retained_kernel=%s retained_components=%s\n' \
      "$kernel_output" "$components" >&2
  fi
  exit "$status"
}
trap cleanup EXIT HUP INT TERM

"$project/system/rc33-system-update/scripts/build-components.sh"   "$source_p1" "$runtime_id" "$components" dedicated-r
"$project/system/rc33-system-update/scripts/build-kernel-candidate.sh"   "$source_p1" "$p1_sha" "$components" "$kernel_output" dedicated-recovery

mkdir "$output/kernel"
cp "$kernel_output/vmlinux.bin" "$kernel_output/vmlinux.bin.gz"   "$kernel_output/readelf-header.txt" "$kernel_output/readelf-program-headers.txt"   "$kernel_output/round27-init" "$kernel_output/recovery-capsule.bin"   "$kernel_output/round84-recovery-capsule.generated.h" "$output/kernel/"
python3 "$project/system/rc33-system-update/scripts/build-kernel-slot.py" \
  --source-slot "$source_slot" --kernel-build "$kernel_output" \
  --name gkd-dedicated-recovery-r --output "$output/recovery-r-slot.bin" \
  --companion "$kernel_output/recovery-capsule.bin" \
  --companion-header "$kernel_output/round84-recovery-capsule.generated.h" \
  --slot-offset 0x300000
[ "$(stat -c %s "$output/recovery-r-slot.bin")" -eq 6291456 ]
python3 "$project/system/rc33-system-update/scripts/verify-kernel-slot.py" \
  --slot "$output/recovery-r-slot.bin" --raw "$kernel_output/vmlinux.bin" \
  --name gkd-dedicated-recovery-r \
  --companion "$kernel_output/recovery-capsule.bin" \
  --companion-header "$kernel_output/round84-recovery-capsule.generated.h" \
  --slot-offset 0x300000
(cd "$output" && sha256sum recovery-r-slot.bin kernel/recovery-capsule.bin   kernel/vmlinux.bin kernel/vmlinux.bin.gz >SHA256SUMS)
printf 'GKD_AR_R=PASS output=%s\n' "$output"
