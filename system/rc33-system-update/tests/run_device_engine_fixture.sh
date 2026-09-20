#!/bin/sh
set -eu
engine=${1:?engine path required}
root=/tmp/gkdsu-engine-physical
case "$root" in /tmp/gkdsu-engine-physical) ;; *) exit 70;; esac
rm -rf -- "$root"
mkdir -p "$root"
trap 'rm -rf -- /tmp/gkdsu-engine-physical' EXIT HUP INT TERM
dd if=/dev/urandom of="$root/p1" bs=131072 count=1 2>/dev/null
dd if=/dev/urandom of="$root/disk" bs=65536 count=1 2>/dev/null
dd if=/dev/zero of="$root/recovery" bs=524288 count=1 2>/dev/null
dd if=/dev/zero of="$root/target-p1" bs=131072 count=1 2>/dev/null
dd if=/dev/zero of="$root/target-kernel" bs=16384 count=1 2>/dev/null
busybox gzip -c "$root/target-p1" >"$root/target-p1.gz"
cp "$root/p1" "$root/old-p1"
cp "$root/disk" "$root/old-disk"
source_p1=$(sha256sum "$root/p1" | cut -d' ' -f1)
source_kernel=$(dd if="$root/disk" bs=1 skip=8192 count=16384 2>/dev/null |
  sha256sum | cut -d' ' -f1)
target_p1=$(sha256sum "$root/target-p1" | cut -d' ' -f1)
target_kernel=$(sha256sum "$root/target-kernel" | cut -d' ' -f1)
package=$(printf 'physical-fixture' | sha256sum | cut -d' ' -f1)
"$engine" backup "$root/recovery" "$root/p1" "$root/disk" "$package" \
  "$source_p1" "$source_kernel" "$target_p1" "$target_kernel"
"$engine" apply "$root/recovery" "$root/p1" "$root/disk" \
  "$root/target-p1.gz" "$root/target-kernel"
cmp "$root/p1" "$root/target-p1"
dd if="$root/disk" of="$root/applied-kernel" bs=1 skip=8192 count=16384 2>/dev/null
cmp "$root/applied-kernel" "$root/target-kernel"
"$engine" restore "$root/recovery" "$root/p1" "$root/disk"
cmp "$root/p1" "$root/old-p1"
cmp "$root/disk" "$root/old-disk"
echo 'GKDSU_ENGINE_DEVICE_FIXTURE=PASS block_devices_touched=0'
