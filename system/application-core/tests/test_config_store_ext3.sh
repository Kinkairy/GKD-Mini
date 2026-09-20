#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
out=${1:?new scoped output directory required}
case "$out" in /tmp/gkd-mini-public/gkd-app-config-ext3-*) ;; *) exit 2 ;; esac
[ ! -e "$out" ]
mkdir -m 0700 "$out"
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
docker run --rm --network none --privileged \
  -v "$project:/src:ro" -v "$out:/out:rw" \
  "$image" sh -eu -c '
out=$1
work=/tmp/gkd-app-config-ext3
runtime=$work/runtime
setup_mount=$work/setup-mount
disk=$out/p2.ext3
loop=
setup_mounted=0
cleanup()
{
  status=$?
  trap - EXIT HUP INT TERM
  if [ "$setup_mounted" = 1 ]; then
    if umount "$setup_mount"; then setup_mounted=0; else status=1; fi
  fi
  if [ -n "$loop" ]; then
    attached=$(losetup -j "$disk" | sed -n "s/:.*//p")
    associations=$(losetup -j "$disk" | wc -l)
    if [ "$attached" = "$loop" ] && [ "$associations" = 1 ]; then
      printf "LOOP_OWNERSHIP=VERIFIED device=%s backing=%s\n" "$loop" "$disk" >>"$out/cleanup.log"
      if losetup -d "$loop"; then
        printf "LOOP_RELEASED=%s\n" "$loop" >>"$out/cleanup.log"
      else status=1
      fi
    else
      printf "LOOP_RELEASE_REFUSED expected=%s attached=%s associations=%s\n" "$loop" "$attached" "$associations" >>"$out/cleanup.log"
      status=1
    fi
  fi
  exit "$status"
}
trap cleanup EXIT
trap "exit 130" HUP INT TERM
rm -rf -- "$work"
mkdir -m 0700 "$work" "$runtime" "$setup_mount" "$work/bin" "$work/config-run" "$work/config-state"
cp /src/system/application-core/config/application.override.conf "$work/application.override.conf"
chmod 0600 "$work/application.override.conf"
truncate -s 16M "$disk"
if ! loop=$(losetup --find --show "$disk"); then
  [ "$(losetup -j "$disk" | wc -l)" = 1 ] &&
    loop=$(losetup -j "$disk" | sed -n "s/:.*//p")
  exit 1
fi
printf "%s\n" "$loop" >"$out/loop-device.log"
attached=$(losetup -j "$disk" | sed -n "s/:.*//p")
[ "$attached" = "$loop" ]
[ "$(losetup -j "$disk" | wc -l)" = 1 ]
mkfs.ext3 -q "$loop"
mount -t ext3 -o noatime,nosuid,nodev "$loop" "$setup_mount"
setup_mounted=1
mkdir -m 0700 "$setup_mount/local" "$setup_mount/local/etc"
umount "$setup_mount"
setup_mounted=0
flags="-std=gnu99 -O2 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion"
cc $flags /src/system/config-core/device/gkd-config-rename.c -o "$work/bin/gkd-config-rename"
cc $flags -I/src/system/application-core/include -I/src/system/ui-core/include \
  "-DGKD_APP_CONFIG_RUNTIME=\"$runtime\"" \
  "-DGKD_APP_CONFIG_EMBEDDED=\"$work/application.override.conf\"" \
  "-DGKD_APP_CONFIG_P2=\"$loop\"" \
  "-DGKD_APP_CONFIG_EXEC=\"/src/system/config-core/device/gkd-config\"" \
  "-DGKD_APP_CONFIG_GUARD=\"/bin/true\"" \
  "-DGKD_APP_CONFIG_CORE_RUN=\"$work/config-run\"" \
  "-DGKD_APP_CONFIG_CORE_STATE=\"$work/config-state\"" \
  /src/system/application-core/source/gkd-app-config-store.c \
  /src/system/application-core/source/gkd-app-job.c \
  -o "$work/bin/gkd-app-config-store"
export GKD_CONFIG_SCHEMA=/src/system/config-core/schema/gdkmini.schema
export GKD_CONFIG_MERGER=/src/system/config-core/device/gkd-config-merge.awk
export GKD_CONFIG_RUN_DIR=$work/config-run
export GKD_CONFIG_STATE_DIR=$work/config-state
export GKD_CONFIG_RENAME=$work/bin/gkd-config-rename
"$work/bin/gkd-app-config-store" offline-load >"$out/defaults.log" 2>&1
grep -qx "ui_dynamic_effects=enabled" "$work/config-run/current/effective.conf"
[ ! -e "$runtime/config-root" ]
! findmnt -rn -S "$loop" >/dev/null
printf "volume_step=7\nui_dynamic_effects=disabled\n" >"$runtime/config.override.conf"
chmod 0600 "$runtime/config.override.conf"
mount -t ext3 -o noatime,nosuid,nodev "$loop" "$setup_mount"
setup_mounted=1
"$work/bin/gkd-app-config-store" save 3 3<"$setup_mount/local/etc" >"$out/save.log" 2>&1
cmp "$runtime/config.override.conf" "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf"
umount "$setup_mount"
setup_mounted=0
rm -f "$runtime/config.override.conf"
"$work/bin/gkd-app-config-store" offline-load >"$out/override.log" 2>&1
grep -qx "ui_dynamic_effects=disabled" "$work/config-run/current/effective.conf"
grep -qx "volume_step=7" "$work/config-run/current/effective.conf"
expected=$(cat "$work/config-run/current/generation")
[ "${#expected}" = 64 ]
mount -t ext3 -o noatime,nosuid,nodev "$loop" "$setup_mount"
setup_mounted=1
"$work/bin/gkd-app-config-store" settings-save 3 "$expected" 0 5 1 0 \
  3<"$setup_mount/local/etc" >"$out/settings-save.log" 2>&1
[ "$(wc -l <"$out/settings-save.log" | tr -d " ")" = 1 ]
grep -Eq "^GKD_APP_SETTINGS=SAVED generation=[0-9a-f]{64}$" "$out/settings-save.log"
generation=$(sed -n "s/^GKD_APP_SETTINGS=SAVED generation=//p" "$out/settings-save.log")
[ "$generation" != "$expected" ]
[ "$(cat "$work/config-run/current/generation")" = "$generation" ]
[ "$(cat "$work/config-state/current/generation")" = "$generation" ]
cmp "$runtime/config.override.conf" "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf"
grep -qx "volume_step=7" "$runtime/config.override.conf"
grep -qx "ui_dynamic_effects=disabled" "$runtime/config.override.conf"
grep -qx "auto_suspend_timeout_seconds=300" "$runtime/config.override.conf"
grep -qx "ui_show_fps=enabled" "$runtime/config.override.conf"
grep -qx "ui_language=en" "$runtime/config.override.conf"
runtime_before=$(sha256sum "$runtime/config.override.conf" | awk "{print \$1}")
p2_before=$(sha256sum "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf" | awk "{print \$1}")
if "$work/bin/gkd-app-config-store" settings-save 3 "$expected" 1 15 0 1 \
  3<"$setup_mount/local/etc" >"$out/settings-stale.log" 2>"$out/settings-stale-diagnostic.log"; then exit 1; fi
[ "$(wc -l <"$out/settings-stale.log" | tr -d " ")" = 1 ]
grep -Eq "^GKD_APP_SETTINGS=FAILED state=recoverable errno=[0-9]+$" "$out/settings-stale.log"
grep -Eq "^GKD_APP_SETTINGS_DIAGNOSTIC state=recoverable errno=[0-9]+$" "$out/settings-stale-diagnostic.log"
[ "$(sha256sum "$runtime/config.override.conf" | awk "{print \$1}")" = "$runtime_before" ]
[ "$(sha256sum "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf" | awk "{print \$1}")" = "$p2_before" ]
umount "$setup_mount"
setup_mounted=0
rm -f "$runtime/config.override.conf"
"$work/bin/gkd-app-config-store" offline-load >"$out/settings-reload.log" 2>&1
grep -qx "volume_step=7" "$work/config-run/current/effective.conf"
grep -qx "auto_suspend_timeout_seconds=300" "$work/config-run/current/effective.conf"
grep -qx "ui_show_fps=enabled" "$work/config-run/current/effective.conf"
grep -qx "ui_language=en" "$work/config-run/current/effective.conf"
before=$(sha256sum "$work/config-run/current/effective.conf" | awk "{print \$1}")
[ ! -e "$runtime/config-root" ]
! findmnt -rn -S "$loop" >/dev/null
mount -t ext3 -o noatime,nosuid,nodev "$loop" "$setup_mount"
setup_mounted=1
printf "ui_dynamic_effects=broken\n" >"$setup_mount/local/etc/gkd-mini/gdkmini.override.conf"
chmod 0600 "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf"
sync "$setup_mount/local/etc/gkd-mini/gdkmini.override.conf"
umount "$setup_mount"
setup_mounted=0
if "$work/bin/gkd-app-config-store" offline-load >"$out/corrupt.log" 2>&1; then exit 1; fi
after=$(sha256sum "$work/config-run/current/effective.conf" | awk "{print \$1}")
[ "$before" = "$after" ]
grep -qx "ui_dynamic_effects=disabled" "$work/config-run/current/effective.conf"
grep -q "GKD_CONFIG_REJECTED reason=invalid_value key=ui_dynamic_effects" "$out/corrupt.log"
[ ! -e "$runtime/config-root" ]
! findmnt -rn -S "$loop" >/dev/null
printf "GKD_APP_CONFIG_EXT3=PASS defaults/save/override/settings-transaction/reload/corrupt-reject/unmounted\n" | tee "$out/result.log"
' sh /out
loop=$(cat "$out/loop-device.log")
if losetup "$loop" >/dev/null 2>&1; then exit 1; fi
cat "$out/defaults.log"
cat "$out/save.log"
cat "$out/override.log"
cat "$out/settings-save.log"
cat "$out/settings-stale.log"
cat "$out/settings-stale-diagnostic.log"
cat "$out/settings-reload.log"
cat "$out/corrupt.log"
cat "$out/cleanup.log"
cat "$out/result.log"
sha256sum "$project/system/application-core/source/gkd-app-config-store.c" \
  "$project/system/application-core/tests/test_config_store_ext3.sh" \
  "$out/defaults.log" "$out/save.log" "$out/override.log" \
  "$out/settings-save.log" "$out/settings-stale.log" \
  "$out/settings-stale-diagnostic.log" "$out/settings-reload.log" \
  "$out/corrupt.log" "$out/cleanup.log" "$out/p2.ext3" >"$out/SHA256SUMS"
cat "$out/SHA256SUMS"
