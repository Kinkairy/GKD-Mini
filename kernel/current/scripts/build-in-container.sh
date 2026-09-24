#!/bin/sh
set -eu

output=${1:?output required}
project=/workspace
current=$project/kernel/current
source_repo=/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1
toolroot=/opt/gkd-build/.cache/gkd-mini-v1.2/ingenic-toolchain-v5.2
cross=$toolroot/toolchain/bin/mips-linux-gnu-
commit=91fe78280ac7dd0dae0f58cb271e821bd39ba97e
patch=$current/patches/0001-rc34-accepted-kernel.patch
application_patch=$current/patches/0002-application-user-plane.patch
application_power_patch=$current/patches/0003-application-power-supply.patch
application_freeze_patch=$current/patches/0004-application-debug-freeze.patch
application_audio_resume_patch=$current/patches/0005-application-audio-resume.patch
application_dma_patch=$current/patches/0006-application-dma-compositor.patch
application_board_identity_patch=$current/patches/0007-application-board-identity.patch
config=$current/config/rc34.config
init_source=${GKD_CURRENT_INIT_SOURCE:?init source required}
recovery_header=${GKD_RECOVERY_CAPSULE_HEADER:-}
build_mode=${GKD_CURRENT_BUILD_MODE:-normal}
variant=${output##*/}
stage=/tmp/gkd-mini-public/gkd-x1830-clean.round39-reproducible
worktree=$stage/source
build=$output/build
init_stage=/tmp/gkd-round27-initramfs.round39-reproducible
init=$init_stage/round27-init
initramfs=$init_stage/round27-initramfs.list

case "$output" in
  /tmp/gkd-mini-public/gkd-kernel-current-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/kernel/current-*) ;;
  *) echo GKD_CURRENT_KERNEL=BLOCKED unsafe-output >&2; exit 2 ;;
esac
case "$stage" in /tmp/gkd-mini-public/gkd-x1830-clean.round39-reproducible) ;; *) exit 2 ;; esac
[ ! -e "$output" ] && [ ! -e "$stage" ] && [ ! -e "$init_stage" ]
[ "$(git -C "$source_repo" rev-parse HEAD)" = "$commit" ]
[ -z "$(git -C "$source_repo" status --porcelain)" ]
[ "$(sha256sum "$patch" | awk '{print $1}')" = 9740d17d106046163fd3e1aca0a32b1b02a85b29a44ea14392670bcf199d8045 ]
[ "$(sha256sum "$config" | awk '{print $1}')" = 9ff2373a77f46ab868b8f472980b57ca100a8729037421f69e927cc9bcf34834 ]
[ -x "${cross}gcc" ]
mkdir -p "$stage" "$output" "$init_stage"

cleanup()
{
  status=$?
  if [ -d "$worktree" ]; then
    git -C "$source_repo" worktree remove --force "$worktree" || true
  fi
  case "$init_stage" in /tmp/gkd-round27-initramfs.*) rm -rf -- "$init_stage" ;; esac
  case "$stage" in /tmp/gkd-mini-public/gkd-x1830-clean.round39-reproducible) rm -rf -- "$stage" ;; esac
  exit "$status"
}
trap cleanup EXIT HUP INT TERM

require_literal()
{
  label=$1
  literal=$2
  file=$3
  if grep -aF -- "$literal" "$file" >/dev/null; then
    printf 'GKD_CURRENT_KERNEL_GATE=%s\n' "$label"
  else
    printf 'GKD_CURRENT_KERNEL=BLOCKED missing=%s file=%s\n' "$label" "$file" >&2
    exit 2
  fi
}

if [ "$build_mode" = application-minimal ]; then
  # Fresh independent files from the verified clean source; XFS COW avoids
  # duplicating 1.3 GiB of unchanged source. Never hardlink mutable build inputs.
  git -C "$source_repo" worktree add --detach --no-checkout "$worktree" "$commit"
  find "$source_repo" -mindepth 1 -maxdepth 1 ! -name .git \
    -exec cp -a --reflink=always --target-directory="$worktree" -- {} +
  git -C "$worktree" read-tree "$commit"
  [ -z "$(git -C "$worktree" status --porcelain)" ]
else
  git -C "$source_repo" worktree add --detach "$worktree" "$commit"
fi
git -C "$worktree" apply --check "$patch"
git -C "$worktree" apply "$patch"
# Both A and R use the same RTC validity contract after display resume.
git -C "$worktree" apply --check "$current/patches/0010-rtc-resume-marker.patch"
git -C "$worktree" apply "$current/patches/0010-rtc-resume-marker.patch"
if [ "$build_mode" = application-minimal ]; then
  install -m 0644 "$project/system/ui-core/include/gkd-ui-plane.h" \
    "$worktree/include/uapi/linux/gkd-ui-plane.h"
  install -m 0644 "$project/system/ui-core/include/gkd-ui-pixels.h" \
    "$worktree/include/linux/gkd-ui-pixels.h"
  git -C "$worktree" apply --recount --check "$application_patch"
  git -C "$worktree" apply --recount "$application_patch"
  git -C "$worktree" apply --check "$application_power_patch"
  git -C "$worktree" apply "$application_power_patch"
  git -C "$worktree" apply --check "$application_freeze_patch"
  git -C "$worktree" apply "$application_freeze_patch"
  git -C "$worktree" apply --check "$application_audio_resume_patch"
  git -C "$worktree" apply "$application_audio_resume_patch"
  git -C "$worktree" apply --check "$application_dma_patch"
  git -C "$worktree" apply "$application_dma_patch"
  git -C "$worktree" apply --check "$application_board_identity_patch"
  git -C "$worktree" apply "$application_board_identity_patch"
  install -m 0644 "$project/system/application-core/include/gkd-menu-vt.h" "$worktree/include/uapi/linux/gkd-menu-vt.h"
  install -m 0644 "$current/menu/gkd-menu-vt.inc" "$worktree/drivers/tty/vt/gkd-menu-vt.inc"
  git -C "$worktree" apply --check "$current/patches/0008-application-menu-vt.patch"
  git -C "$worktree" apply "$current/patches/0008-application-menu-vt.patch"
  git -C "$worktree" apply --check "$current/patches/0009-application-triple-buffer.patch"
  git -C "$worktree" apply "$current/patches/0009-application-triple-buffer.patch"
fi
git -C "$worktree" diff --check

sealed_flag=
dedicated_flag=
recovery_include=
if [ -n "$recovery_header" ]; then
  sealed_flag=-DR64M_SEALED_RECOVERY=1
  recovery_include=-I${recovery_header%/*}
fi
if [ "$build_mode" = dedicated-recovery ] || [ "$build_mode" = application-minimal ]; then
  dedicated_flag=-DR64M_DEDICATED_RECOVERY=1
fi
"${cross}gcc" -Os -s \
  -DGKD_ROUND28_DETAILED_FB=1 \
  $sealed_flag $dedicated_flag \
  -fno-asynchronous-unwind-tables -fno-ident -fno-stack-protector \
  -ffunction-sections -fdata-sections -nostdlib -static \
  -Wl,--build-id=none -Wl,--gc-sections \
  -I"$source_repo/tools/include/nolibc" -I"$current/initramfs" $recovery_include \
  "$init_source" -lgcc -o "$init"
touch -d @1785448800 "$init"
{
  printf '%s\n' '# Deterministic current GKD Mini built-in initramfs manifest.'
  printf '%s\n' 'dir /dev 0755 0 0'
  printf '%s\n' 'nod /dev/console 0600 0 0 c 5 1'
  printf '%s\n' 'nod /dev/mmcblk0 0600 0 0 b 179 0'
  printf '%s\n' 'nod /dev/mmcblk0p1 0600 0 0 b 179 1'
  printf '%s\n' 'dir /newroot 0755 0 0'
  printf '%s\n' 'dir /proc 0555 0 0'
  printf '%s\n' 'dir /sys 0555 0 0'
  printf 'file /init %s 0755 0 0\n' "$init"
  printf '%s\n' 'slink /linuxrc init 0755 0 0'
} >"$initramfs"
mkdir -p "$build"
cp "$config" "$build/.config"
"$worktree/scripts/config" --file "$build/.config" --set-str INITRAMFS_SOURCE "$initramfs"
"$worktree/scripts/config" --file "$build/.config" --enable INITRAMFS_FORCE
"$worktree/scripts/config" --file "$build/.config" --disable INITRAMFS_PRESERVE_MTIME
cp "$build/.config" "$build/gkd-profile-base.config"
python3 "$current/scripts/configure-profile.py" apply --mode "$build_mode" \
  --base "$build/gkd-profile-base.config" --config "$build/.config"

export ARCH=mips CROSS_COMPILE=$cross
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=c-builder KBUILD_BUILD_VERSION=3
export KBUILD_BUILD_TIMESTAMP='2026-07-31 06:00:00 +0800'
debug_root=/build/gkd-mini-round39
debug_flags="-gno-record-gcc-switches -fno-record-gcc-switches -fdebug-prefix-map=$build=$debug_root"
export KCFLAGS="$debug_flags" KAFLAGS="$debug_flags" LOCALVERSION=
make -C "$worktree" O="$build" olddefconfig
python3 "$current/scripts/configure-profile.py" verify --mode "$build_mode" \
  --base "$build/gkd-profile-base.config" --config "$build/.config"
make -C "$worktree" O="$build" -j"$(nproc)" vmlinux dtbs vmlinux.bin modules

cp "$build/arch/mips/boot/vmlinux.bin" "$output/vmlinux.bin"
if [ "$build_mode" = application-minimal ]; then
  # Standard gzip remains the boot ABI; pin the stronger encoder source.
  python3 "$current/scripts/compress-kernel.py" \
    "$output/vmlinux.bin" "$output/vmlinux.bin.gz" \
    /opt/gkd-build/private-state/gkd-mini-system-rebuild/rc3.6-build-inputs/zopfli-1.0.3.tar.gz
else
  gzip -9 -n -c "$output/vmlinux.bin" >"$output/vmlinux.bin.gz"
fi
cp "$build/drivers/input/misc/uinput.ko" "$output/uinput.ko"
cp "$build/arch/mips/boot/dts/ingenic/gkd350.dtb" "$output/gkd350.dtb"
cp "$build/.config" "$output/gkd350.config"
cp "$init" "$output/round27-init"
"${cross}readelf" -h "$build/vmlinux" >"$output/readelf-header.txt"
"${cross}readelf" -l "$build/vmlinux" >"$output/readelf-program-headers.txt"

[ "$(cat "$build/include/config/kernel.release")" = 6.1.28-gkd-rc29-visual-r12-0138 ]
printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=release'
case "$build_mode" in
  normal)
    require_literal normal-debug-return 'GKD_DEBUG_RETURN=1' "$output/round27-init"
    require_literal normal-update-hold '8P13' "$output/round27-init"
    printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=normal-init'
    ;;
  dedicated-recovery)
    require_literal recovery-hold-code RCVR "$output/round27-init"
    require_literal recovery-entry-banner 'GKD independent RAM recovery entered' "$output/round27-init"
    require_literal recovery-ui-path '/usr/sbin/gkd-recovery-ui' "$output/round27-init"
    printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=recovery-init'
    ;;
  application-minimal)
    require_literal application-hold-code APPA "$output/round27-init"
    require_literal application-entry 'GKD RC3.6 A entered' "$output/round27-init"
    require_literal application-service '/usr/sbin/gkd-application-start' "$output/round27-init"
    require_literal application-hwrng-driver 'Ingenic DTRNG driver registered' "$build/vmlinux"
    require_literal application-hwrng-credit 'rng_core.default_quality=1024' "$build/vmlinux"
    ! grep -aF 'gkd-round64-rescue-ui' "$output/round27-init" >/dev/null
    ! grep -aF '/usr/sbin/gkd-recovery-ui' "$output/round27-init" >/dev/null
    [ "$(sed -n 's/^CONFIG_FB_X1830_USER_PLANE=//p' "$output/gkd350.config")" = y ]
    [ "$(sed -n 's/^CONFIG_FB_INGENIC_X1830_DPU=//p' "$output/gkd350.config")" = y ]
    require_literal application-user-plane gkd_ui_plane_submit_ioctl "$build/vmlinux"
    ! grep -aF gkd_volume_osd_store "$build/vmlinux" >/dev/null
    ! grep -aF gkd_brightness_osd_store "$build/vmlinux" >/dev/null
    ! grep -aF gkd_notification_store "$build/vmlinux" >/dev/null
    ! grep -aF gkd_usb_menu_store "$build/vmlinux" >/dev/null
    ! grep -aF gkd_power_osd_store "$build/vmlinux" >/dev/null
    printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=application-minimal'
    ;;
  *)
    echo GKD_CURRENT_KERNEL=BLOCKED invalid-build-mode >&2
    exit 2
    ;;
esac
require_literal framebuffer-abi 'GKD Round28: RGB565 framebuffer ABI failed (FBER detail)' "$output/round27-init"
require_literal usb-debug-symbol usb_debug_loading_target "$build/vmlinux"
require_literal usb-debug-timeout 'GKD DEBUG Loading hold timed out' "$build/vmlinux"
printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=kernel-symbols'
[ "$(sha256sum "$output/uinput.ko" | awk '{print $1}')" = 64260225444c49f9940918a4ca49e83aa47bfa2f0315626fbb3324abc61bad51 ]
printf '%s\n' 'GKD_CURRENT_KERNEL_GATE=uinput'
raw=$(stat -c %s "$output/vmlinux.bin")
packed=$(stat -c %s "$output/vmlinux.bin.gz")
if [ "$raw" -ge 8388608 ] || [ "$packed" -ge 6291392 ]; then
  printf 'GKD_CURRENT_KERNEL=BLOCKED raw_bytes=%s gzip_bytes=%s\n' "$raw" "$packed" >&2
  exit 2
fi
(cd "$output" && sha256sum gkd350.config gkd350.dtb readelf-header.txt \
  readelf-program-headers.txt round27-init uinput.ko vmlinux.bin vmlinux.bin.gz >SHA256SUMS)
printf 'GKD_CURRENT_KERNEL=PASS raw_bytes=%s gzip_bytes=%s output=%s\n' "$raw" "$packed" "$output"
