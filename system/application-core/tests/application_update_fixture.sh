#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
script=$lane/device/gkd-application-update
root=$(mktemp -d /tmp/gkd-mini-public/gkd-application-update-test.XXXXXX)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/bin" "$root/state" "$root/operations" "$root/game/gkd-update"
disk=$root/disk
recovery=$root/recovery
p1=$root/p1
game_device=$root/game-device
runtime=$root/runtime-id
calls=$root/calls
target=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
package=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
source=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
: >"$disk"; : >"$p1"; : >"$game_device"; : >"$root/lun"
printf 'package\n' >"$root/game/gkd-update/system.gkdupdate"

clean_swap()
{
	dd if=/dev/zero of="$recovery" bs=8192 count=1 2>/dev/null
	printf '\001' | dd of="$recovery" bs=1 seek=1024 conv=notrunc 2>/dev/null
	printf '\377\377\003\000' | dd of="$recovery" bs=1 seek=1028 conv=notrunc 2>/dev/null
	printf '0123456789abcdefGKD-SWAP' |
		dd of="$recovery" bs=1 seek=1036 conv=notrunc 2>/dev/null
	printf 'SWAPSPACE2' | dd of="$recovery" bs=1 seek=4086 conv=notrunc 2>/dev/null
}

cat >"$root/bin/tool" <<'TOOL'
#!/bin/sh
set -eu
state=${GKD_APPLICATION_UPDATE_TEST_STATE_ROOT:?}
name=${0##*/}
printf '%s' "$name" >>"$TEST_CALLS"
for value in "$@"; do printf ' <%s>' "$value" >>"$TEST_CALLS"; done
printf '\n' >>"$TEST_CALLS"
case "$name" in
 guard)
	printf 'LEAF guard idle\n'
	[ ! -e "$state/guard-fail" ]
	;;
 request)
	[ "$1" = show ] || exit 2
	case "$(cat "$state/request")" in
	 valid)
		printf 'package_sha256=%s\n' "$TEST_PACKAGE"
		printf 'source_runtime_id=%s\n' "$TEST_SOURCE"
		printf 'target_runtime_id=%s\n' "$TEST_TARGET"
		printf '%s\n' 'package_bytes=8' 'p1_payload_offset=0' 'p1_payload_bytes=1' 'kernel_payload_offset=1' 'kernel_payload_bytes=7'
		;;
	 idle) exit 3 ;;
	 *) printf 'LEAF request corrupt\n'; exit 2 ;;
	esac
	;;
 engine)
	case "$1" in
	 inspect)
		case "$(cat "$state/journal")" in
		 [1-9]) printf 'GKDSU_INSPECT=PASS sequence=1 state=%s package=%s\n' "$(cat "$state/journal")" "$TEST_PACKAGE" ;;
		 *) printf 'LEAF journal unavailable\n'; exit 1 ;;
		esac
		;;
	 trial-good)
		printf 'LEAF engine trial-good\n'
		[ ! -e "$state/identity-fail" ] || exit 2
		[ "$(cat "$state/journal")" = 9 ] || exit 2
		printf '7\n' >"$state/journal"
		;;
	 *) exit 2 ;;
	esac
	;;
 coordinator)
	printf 'LEAF coordinator\n'
	case "$(cat "$state/coordinator")" in
	 0) printf '9\n' >"$state/journal"; exit 0 ;;
	 10) printf '5\n' >"$state/journal"; exit 10 ;;
	 20)
		dd if=/dev/zero of="$2" bs=8192 count=1 2>/dev/null
		printf '\001' | dd of="$2" bs=1 seek=1024 conv=notrunc 2>/dev/null
		printf '\377\377\003\000' | dd of="$2" bs=1 seek=1028 conv=notrunc 2>/dev/null
		printf 'SWAPSPACE2' | dd of="$2" bs=1 seek=4086 conv=notrunc 2>/dev/null
		printf 'idle\n' >"$state/journal"; printf 'idle\n' >"$state/request"; exit 20
		;;
	 format-fail) exit 2 ;;
	 clear-fail)
		dd if=/dev/zero of="$2" bs=8192 count=1 2>/dev/null
		printf '\001' | dd of="$2" bs=1 seek=1024 conv=notrunc 2>/dev/null
		printf '\377\377\003\000' | dd of="$2" bs=1 seek=1028 conv=notrunc 2>/dev/null
		printf 'SWAPSPACE2' | dd of="$2" bs=1 seek=4086 conv=notrunc 2>/dev/null
		printf 'idle\n' >"$state/journal"; exit 2
		;;
	 *) exit 2 ;;
	esac
	;;
 mount) printf 'LEAF mount\n'; [ ! -e "$state/mount-fail" ] ;;
 umount) printf 'LEAF umount\n'; [ ! -e "$state/umount-fail" ] ;;
 *) exit 2 ;;
esac
TOOL
chmod 755 "$root/bin/tool"
for name in guard request engine coordinator mount umount; do
	ln -s "$root/bin/tool" "$root/bin/$name"
done

run_update()
{
	set +e
	GKD_APPLICATION_UPDATE_TESTING=1 GKD_APPLICATION_UPDATE_TEST_DISK="$disk" GKD_APPLICATION_UPDATE_TEST_RECOVERY="$recovery" GKD_APPLICATION_UPDATE_TEST_P1="$p1" GKD_APPLICATION_UPDATE_TEST_GAME_DEVICE="$game_device" GKD_APPLICATION_UPDATE_TEST_GAME_MOUNT="$root/game" GKD_APPLICATION_UPDATE_TEST_RUNTIME_ID="$runtime" GKD_APPLICATION_UPDATE_TEST_STATE_ROOT="$root/state" GKD_APPLICATION_UPDATE_TEST_OPERATION_ROOT="$root/operations" GKD_APPLICATION_UPDATE_TEST_LUN_FILE="$root/lun" GKD_APPLICATION_UPDATE_TEST_GUARD="$root/bin/guard" GKD_APPLICATION_UPDATE_TEST_REQUEST_TOOL="$root/bin/request" GKD_APPLICATION_UPDATE_TEST_ENGINE="$root/bin/engine" GKD_APPLICATION_UPDATE_TEST_COORDINATOR="$root/bin/coordinator" GKD_APPLICATION_UPDATE_TEST_MOUNT="$root/bin/mount" GKD_APPLICATION_UPDATE_TEST_UMOUNT="$root/bin/umount" TEST_CALLS="$calls" TEST_TARGET="$target" TEST_PACKAGE="$package" TEST_SOURCE="$source" sh "$script" "$1" >"$root/stdout" 2>"$root/stderr"
	rc=$?
	set -e
}

reset_case()
{
	rm -f "$root/state/update-trial" "$root/state/guard-fail" "$root/state/identity-fail" "$root/state/mount-fail" "$root/state/umount-fail" "$root/operations/card-operation.lock"
	: >"$calls"; : >"$root/stdout"; : >"$root/stderr"; : >"$root/lun"
	printf 'valid\n' >"$root/state/request"
	printf 'idle\n' >"$root/state/journal"
	printf '2\n' >"$root/state/coordinator"
	chmod 0644 "$runtime" 2>/dev/null || true
	printf '%s\n' "$target" >"$runtime"
	chmod 0444 "$runtime"
	clean_swap
}

expect_failure()
{
	[ "$rc" -eq 1 ]
	[ ! -s "$root/stdout" ]
	grep -Fq "GKD_APPLICATION_UPDATE=BLOCKED reason=$1" "$root/stderr"
	[ ! -e "$root/operations/card-operation.lock" ]
}

trial_context()
{
	reset_case
	printf '5\n' >"$root/state/journal"
	printf '0\n' >"$root/state/coordinator"
	rm -f "$root/game/gkd-update/system.gkdupdate" "$game_device"
	run_update boot
	printf 'package\n' >"$root/game/gkd-update/system.gkdupdate"
	: >"$game_device"
	[ "$rc" -eq 0 ] && [ -f "$root/state/update-trial" ]
}

reset_case
printf 'idle\n' >"$root/state/request"
run_update boot
[ "$rc" -eq 0 ]
grep -Fxq 'GKD_APPLICATION_UPDATE=BOOT outcome=normal' "$root/stdout"
grep -Fq 'LEAF guard idle' "$root/stderr"
! grep -q '^coordinator ' "$calls"

reset_case
printf 'idle\n' >"$root/state/request"
printf 'corrupt\n' >"$root/state/journal"
printf X | dd of="$recovery" bs=1 seek=0 conv=notrunc 2>/dev/null
printf Y | dd of="$recovery" bs=1 seek=4096 conv=notrunc 2>/dev/null
run_update boot
expect_failure corrupt-idle-state
! grep -q '^coordinator ' "$calls"

reset_case
printf 'corrupt\n' >"$root/state/journal"
printf X | dd of="$recovery" bs=1 seek=0 conv=notrunc 2>/dev/null
printf Y | dd of="$recovery" bs=1 seek=4096 conv=notrunc 2>/dev/null
run_update boot
expect_failure corrupt-update-state
! grep -q '^coordinator ' "$calls"
! grep -q '^mount ' "$calls"

reset_case
printf '10\n' >"$root/state/coordinator"
run_update boot
[ "$rc" -eq 10 ]
grep -Fxq 'GKD_APPLICATION_UPDATE=BOOT outcome=reboot coordinator=10' "$root/stdout"
grep -q '^mount .*<-t> <vfat> <-o> <ro,nosuid,nodev,noexec,utf8>' "$calls"
grep -q '^coordinator </proc/self/fd/3/gkd-update/system.gkdupdate>' "$calls"
grep -q '^umount ' "$calls"

reset_case
printf '1\n' >"$root/state/journal"
printf '10\n' >"$root/state/coordinator"
run_update boot
[ "$rc" -eq 10 ]
grep -Fxq 'GKD_APPLICATION_UPDATE=BOOT outcome=reboot coordinator=10' "$root/stdout"
grep -q '^mount ' "$calls"
grep -q '^coordinator </proc/self/fd/3/gkd-update/system.gkdupdate>' "$calls"
grep -q '^umount ' "$calls"

reset_case
rm "$root/game/gkd-update/system.gkdupdate" "$game_device"
printf '9\n' >"$root/state/journal"
printf '20\n' >"$root/state/coordinator"
run_update boot
[ "$rc" -eq 20 ]
grep -Fxq 'GKD_APPLICATION_UPDATE=BOOT outcome=reboot coordinator=20' "$root/stdout"
! grep -q '^mount ' "$calls"
printf 'package\n' >"$root/game/gkd-update/system.gkdupdate"
: >"$game_device"

reset_case
rm "$root/game/gkd-update/system.gkdupdate" "$game_device"
printf '7\n' >"$root/state/journal"
printf '20\n' >"$root/state/coordinator"
run_update boot
[ "$rc" -eq 20 ]
! grep -q '^mount ' "$calls"
printf 'package\n' >"$root/game/gkd-update/system.gkdupdate"
: >"$game_device"

trial_context
grep -Fxq "GKD_APPLICATION_UPDATE=BOOT outcome=trial target=$target" "$root/stdout"
[ "$(stat -c %a "$root/state/update-trial")" = 600 ]
grep -Fxq "package_sha256=$package" "$root/state/update-trial"
grep -Fxq "target_runtime_id=$target" "$root/state/update-trial"
! grep -q '^mount ' "$calls"

reset_case
printf '5\n' >"$root/state/journal"
printf '0\n' >"$root/state/coordinator"
chmod 0644 "$runtime"
printf '%s\n' "$source" >"$runtime"
chmod 0444 "$runtime"
run_update boot
expect_failure target-runtime-mismatch
[ ! -e "$root/state/update-trial" ]
[ "$(cat "$root/state/journal")" = 9 ]

reset_case
printf '6\n' >"$root/state/journal"
run_update boot
expect_failure coordinator
! grep -q '^mount ' "$calls"

trial_context
printf '/dev/mmcblk1\n' >"$root/lun"
: >"$calls"
run_update mark-good
expect_failure trial-media-busy-or-exported
[ -f "$root/state/update-trial" ]
grep -q '^guard <mmcblk0p3>' "$calls"
! grep -q '^engine <trial-good>' "$calls"

trial_context
: >"$root/state/identity-fail"
: >"$calls"
run_update mark-good
expect_failure trial-good
[ -f "$root/state/update-trial" ]
! grep -q '^coordinator ' "$calls"

trial_context
printf 'format-fail\n' >"$root/state/coordinator"
: >"$calls"
run_update mark-good
expect_failure finalize
[ "$(cat "$root/state/journal")" = 7 ]
[ "$(cat "$root/state/request")" = valid ]
[ -f "$root/state/update-trial" ]

trial_context
printf 'clear-fail\n' >"$root/state/coordinator"
: >"$calls"
run_update mark-good
expect_failure finalize
[ "$(cat "$root/state/journal")" = idle ]
[ "$(cat "$root/state/request")" = valid ]
[ -f "$root/state/update-trial" ]

trial_context
printf '20\n' >"$root/state/coordinator"
run_update mark-good
[ "$rc" -eq 0 ]
grep -Fxq "GKD_APPLICATION_UPDATE=MARK_GOOD outcome=complete target=$target" "$root/stdout"
[ ! -e "$root/state/update-trial" ]
[ "$(cat "$root/state/request")" = idle ]
[ "$(cat "$root/state/journal")" = idle ]

trial_context
printf '7\n' >"$root/state/journal"
printf '20\n' >"$root/state/coordinator"
: >"$calls"
run_update mark-good
[ "$rc" -eq 0 ]
! grep -q '^engine <trial-good>' "$calls"
grep -q '^coordinator ' "$calls"

trial_context
chmod 0644 "$runtime"
printf '%s\n' "$source" >"$runtime"
chmod 0444 "$runtime"
run_update mark-good
expect_failure trial-binding
[ -f "$root/state/update-trial" ]

reset_case
: >"$root/state/mount-fail"
printf '10\n' >"$root/state/coordinator"
run_update boot
expect_failure package-mount-or-missing
! grep -q '^coordinator ' "$calls"

reset_case
: >"$root/state/umount-fail"
printf '10\n' >"$root/state/coordinator"
run_update boot
expect_failure package-unmount
grep -q '^coordinator ' "$calls"

reset_case
printf 'invalid\n' >"$root/state/request"
run_update boot
expect_failure invalid-request

reset_case
: >"$root/state/guard-fail"
run_update boot
expect_failure system-card-busy-or-unreadable
! grep -q '^request ' "$calls"

reset_case
mkdir "$root/operations/card-operation.lock"
run_update boot
[ "$rc" -eq 1 ] && [ ! -s "$root/stdout" ]
grep -Fq 'GKD_APPLICATION_UPDATE=BLOCKED reason=operation-busy' "$root/stderr"
[ -d "$root/operations/card-operation.lock" ]
rmdir "$root/operations/card-operation.lock"

! grep -En '(^|[[:space:];|&])reboot([[:space:];|&]|$)|pidof|gkd-system-update|gkd-recovery-ui' "$script" >/dev/null
[ "$(wc -c <"$root/stdout")" -lt 512 ]
printf '%s\n' 'GKD_APPLICATION_UPDATE_FIXTURE=PASS idle=1 corrupt=2 apply-rc10=2 trial=1 rollback-no-sd=1 finalize-no-sd=1 bad-runtime=1 mark-good-success=1 mark-good-identity=1 mark-good-format=1 mark-good-clear=1 no-lun=1 resume-finalize=1 mount-fail=1 unmount-fail=1 guard=1 lock=1'
