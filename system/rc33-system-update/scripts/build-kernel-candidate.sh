#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
p1=${1:?source P1 required}
p1_sha=${2:?source P1 sha256 required}
components=${3:?component directory required}
out=${4:?candidate output required}
mode=${5:-normal}

case "$mode" in
  normal) derive_mode=; capsule_layout=fixed; ui_profile=normal ;;
  dedicated-recovery) derive_mode=--dedicated-recovery; capsule_layout=dedicated-r; ui_profile=dedicated-r ;;
  *) echo GKDSU_KERNEL=BLOCKED invalid-mode >&2; exit 2 ;;
esac

case "$out" in
  /tmp/gkd-mini-public/gkd-kernel-current-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/kernel/current-*) ;;
  *) echo GKDSU_KERNEL=BLOCKED unsafe-output >&2; exit 2 ;;
esac
[ ! -e "$out" ]
[ -f "$p1" ] && [ ! -L "$p1" ]
[ "$(sha256sum "$p1" | awk '{print $1}')" = "$p1_sha" ]
[ -f "$components/gkd-update-runtime-static" ]

schema_keys=$(awk -F'|' '!/^#/ && NF == 8 {count++} END {print count+0}' "$project/system/config-core/schema/gdkmini.schema")
[ "$schema_keys" -gt 0 ]

tmp=$(mktemp -d /tmp/gkd-mini-public/gkd-current-init-update.XXXXXX)
ui=/tmp/gkd-mini-public/gkd-ui-r-capsule.$$
trap 'rm -rf -- "$tmp" "$ui"' EXIT HUP INT TERM

"$project/system/ui-core/scripts/build-mips.sh" "$ui" "$ui_profile"

if [ "$mode" = dedicated-recovery ]; then
  python3 "$project/kernel/current/scripts/verify-ram-closure.py" \
    --p1 "$p1" --temporary "$tmp/closure-readback" \
    --derived-header "$tmp/round83-ram-payload.generated.h" \
    --extra-manifest "$project/kernel/current/initramfs/dedicated-r-p1-manifest.json" \
    --exclude-regular /usr/lib/gkd-usb-round64/gkd-round64-rescue-ui \
    --exclude-regular /etc/gkd-mini/gdkmini.conf \
    --exclude-regular /etc/init.d/S94gkd-screenshot \
    --exclude-regular /usr/sbin/gkd-screenshot
  GKD_CONFIG_SCHEMA="$project/system/config-core/schema/gdkmini.schema" \
  GKD_CONFIG_MERGER="$project/system/config-core/device/gkd-config-merge.awk" \
  GKD_CONFIG_OVERRIDE="$project/system/config-core/profiles/dedicated-r.override.conf" \
  GKD_CONFIG_RUN_DIR="$tmp/config-run" \
  GKD_CONFIG_STATE_DIR="$tmp/config-state" \
    "$project/system/config-core/device/gkd-config" apply
  cp "$tmp/config-run/current/effective.conf" "$tmp/gdkmini-r.conf"
  chmod -R u+w "$tmp/config-run" "$tmp/config-state"
  [ "$(wc -l <"$tmp/gdkmini-r.conf" | tr -d ' ')" -eq "$schema_keys" ]
  python3 "$project/system/ui-core/scripts/generate-boot-art.py" \
    --source "$project/system/ui-core/assets/recovery-boot.png" \
    --config "$tmp/gdkmini-r.conf" --output "$tmp/gkd-boot-art.generated.h"
  cp "$project/system/ui-core/include/gkd-boot-frame.h" \
    "$project/kernel/current/initramfs/dedicated-r-boot.h" "$tmp/"
else
  python3 "$project/kernel/current/scripts/verify-ram-closure.py" \
    --p1 "$p1" --temporary "$tmp/closure-readback"
  cp "$project/kernel/current/initramfs/round83-ram-payload.generated.h" "$tmp/"
fi
python3 "$lane/scripts/generate-ram-runtime.py" --p1 "$p1" \
  --p1-sha256 "$p1_sha" --components "$components" \
  --output "$tmp/round84-update-runtime.generated.h"
python3 "$lane/scripts/derive-update-supervisor.py" \
  --source "$project/kernel/current/initramfs/round83-supervisor.c" \
  --output "$tmp/round84-supervisor.generated.c" $derive_mode
cp "$project/kernel/current/initramfs/round83-maintenance-loader.generated.h" \
  "$project/kernel/current/initramfs/round64-request-gate.h" \
  "$project/kernel/current/initramfs/round38-animation-format.h" "$tmp/"

python3 "$lane/scripts/build-recovery-capsule.py" \
  --manifest "$project/kernel/current/initramfs/ram-payload-manifest.json" \
  --verified-root "$tmp/closure-readback" --components "$components" \
  --recovery-script "$project/kernel/current/initramfs/gkd-recovery" \
  --mass-storage-script "$project/kernel/current/initramfs/gkd-recovery-mass-storage" \
  --recovery-usb-script "$project/kernel/current/initramfs/gkd-recovery-usb" \
  --ui-binary "$ui/gkd-recovery-ui" \
  --input-binary "$ui/gkd-input" \
  --input-init "$ui/S95gkd-input" \
  --screenshot-binary "$ui/gkd-screenshot" \
  --system-config "$tmp/gdkmini-r.conf" \
  --ui-config "$ui/gdkmini-ui.defaults.conf" \
  --ui-font "$ui/fallback.psf" \
  --temporary "$tmp/recovery-root" \
  --output "$tmp/recovery-capsule.bin" \
  --header "$tmp/round84-recovery-capsule.generated.h" \
  --loader "$project/kernel/current/initramfs/round83-maintenance-loader.generated.h" \
  --layout "$capsule_layout"

GKD_CURRENT_BUILD_MODE=$mode \
"$project/kernel/current/scripts/build-kernel.sh" \
  "$out" "$tmp/round84-supervisor.generated.c" \
  "$tmp/round84-recovery-capsule.generated.h"
cp "$tmp/recovery-capsule.bin" "$out/recovery-capsule.bin"
cp "$tmp/round84-recovery-capsule.generated.h" \
  "$out/round84-recovery-capsule.generated.h"
grep -aF 'GKD independent RAM recovery entered' "$out/round27-init" >/dev/null
! grep -aF 'GKD system update runtime failed' "$out/round27-init" >/dev/null
! cpio -it <"$out/build/usr/initramfs_data.cpio" 2>/dev/null | \
  grep -Fx 'bin/busybox' >/dev/null
capsule_bytes=$(stat -c %s "$out/recovery-capsule.bin")
if [ "$mode" = normal ]; then
  [ "$capsule_bytes" -eq 5046272 ]
else
  [ "$capsule_bytes" -gt 0 ] && [ "$capsule_bytes" -lt 2490368 ]
  grep -Fx '#define R84_RECOVERY_CAPSULE_OFFSET ((off_t)0x6a0000)' \
    "$out/round84-recovery-capsule.generated.h" >/dev/null
fi
unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
  grep -F 'squashfs-root/usr/sbin/gkd-recovery' >/dev/null
unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
  grep -F 'squashfs-root/usr/sbin/gkd-recovery-ui' >/dev/null
unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
  grep -F 'squashfs-root/lib/libdl-0.9.33.2.so' >/dev/null
unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
  grep -F 'squashfs-root/lib/libdl.so.0 -> libdl-0.9.33.2.so' >/dev/null
unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
  grep -F 'squashfs-root/etc/gkd-mini/fonts/fallback.psf' >/dev/null
grep -aF 'gkd-recovery-capsule' "$out/round27-init" >/dev/null
if [ "$mode" = dedicated-recovery ]; then
  grep -aF 'RCVR' "$out/round27-init" >/dev/null
  grep -aF 'GKD RAM recovery service failed; preserving stage marker and holding' \
    "$out/round27-init" >/dev/null
  ! grep -aF 'gkd-round64-rescue-ui' "$out/round27-init" >/dev/null
  unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/usr/sbin/gkd-recovery-usb' >/dev/null
  unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/usr/sbin/gkd-input' >/dev/null
  unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/etc/init.d/S95gkd-input' >/dev/null
  unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/usr/lib/gkd-uinput.ko' >/dev/null
  unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/usr/sbin/gkd-screenshot' >/dev/null
  ! unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/etc/init.d/S94gkd-screenshot' >/dev/null
  ! unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'squashfs-root/usr/local/etc/gkd-mini/gdkmini.conf' >/dev/null
  unsquashfs -d "$tmp/r-config-extract" "$out/recovery-capsule.bin" \
    etc/gkd-mini/gdkmini.conf >/dev/null
  r_config_readback=$tmp/r-config-extract/etc/gkd-mini/gdkmini.conf
  [ "$(wc -l <"$r_config_readback" | tr -d ' ')" -eq "$schema_keys" ]
  grep -Fx 'screenshot_output_dir=/media/gkd-r-screenshots' \
    "$r_config_readback" >/dev/null
  grep -Fx 'screenshot_osd_timeout_ms=0' "$r_config_readback" >/dev/null
  grep -Fx 'screenshot_hotkey=NONE' "$r_config_readback" >/dev/null
  ! unsquashfs -ll "$out/recovery-capsule.bin" 2>/dev/null | \
    grep -F 'gkd-round64-rescue-ui' >/dev/null
else
  grep -aF 'GKD RAM recovery service failed; holding instead of rebooting' \
    "$out/round27-init" >/dev/null
fi
printf 'GKDSU_KERNEL=PASS mode=%s output=%s\n' "$mode" "$out"
