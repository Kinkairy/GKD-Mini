# R lifecycle adapter around the one authenticated updater and journal engine.
# No A filesystem execution, runtime-id pin, or second transaction implementation.
recovery_update_offline() {
	awk '$1 ~ /^\/dev\/mmcblk0(p[0-9]+)?$/ {bad=1} END {exit bad}' /proc/mounts || return 1
	awk 'NR > 1 {bad=1} END {exit bad}' /proc/swaps || return 1
	for lun in /sys/kernel/config/usb_gadget/*/functions/mass_storage.*/lun.*/file; do
		[ ! -r "$lun" ] || [ -z "$(cat "$lun")" ] || return 1
	done
}

recovery_update_cleanup() {
	if [ "$mounted" = 1 ]; then
		umount "$game_mount" || return 1
		mounted=0
	fi
}

recovery_update_reboot() {
	# PID 1 is the nolibc supervisor, not BusyBox init: a signal is insufficient.
	/bin/busybox reboot -f
}

prepare_update() (
	state_dir=/run/gkd-recovery
	mkdir -p "$state_dir" || exit 2
	mkdir "$state_dir/card-operation.lock" 2>/dev/null || exit 2
	mounted=0
	# An unmount failure retains the lock: neither export nor another writer
	# may start while ownership of that filesystem is unresolved.
	trap 'if recovery_update_cleanup; then rmdir "$state_dir/card-operation.lock"; else echo "GKD_RECOVERY=BLOCKED reason=cleanup lock=retained"; fi' EXIT
	trap 'exit 2' HUP INT TERM
	exec >"$state_dir/update.log" 2>&1
	recovery_update_offline || { echo 'GKD_RECOVERY=BLOCKED reason=not-offline'; exit 2; }
	power_gate || { echo 'GKD_RECOVERY=BLOCKED reason=power'; exit 2; }
	[ -x "$prepare" ] && [ -x "$coordinator" ] && [ -x "$engine" ] &&
		[ -x "$request" ] && [ -r "$public_key" ] || exit 2
	request_status=0
	"$request" show "$disk" >/dev/null 2>&1 || request_status=$?
	case "$request_status" in 0|3) ;; *) echo 'GKD_RECOVERY=BLOCKED reason=request'; exit 2 ;; esac
	if mount_game; then mounted=1
	elif [ "$request_status" = 3 ]; then
		echo 'GKD_RECOVERY=BLOCKED reason=game-card'; exit 2
	fi
	if [ "$request_status" = 3 ]; then
		[ -f "$package" ] &&
			"$prepare" "$package" "$public_key" "$disk" --source-kernel "$p3" || {
			echo 'GKD_RECOVERY=BLOCKED reason=prepare'; exit 2;
		}
	fi
	# With an existing request, the coordinator alone decides resume/restore/
	# finalize. Do not replace it or consume A's TRIAL_PENDING attempt here.
	result=0
	"$coordinator" "$package" "$p3" "$p1" "$disk" "$engine" || result=$?
	case "$result" in
		10|20)
			recovery_update_cleanup || exit 2
			sync
			echo 'GKD_RECOVERY=PASS action=transaction-complete reboot=1'
			recovery_update_reboot
			echo 'GKD_RECOVERY=BLOCKED reason=reboot-returned'; exit 2
			;;
		*) echo "GKD_RECOVERY=BLOCKED reason=coordinator status=$result"; exit 2 ;;
	esac
)
