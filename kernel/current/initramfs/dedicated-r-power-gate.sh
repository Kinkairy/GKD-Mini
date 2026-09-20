# Dedicated R adapter. The PMIC probe is the unchanged accepted A implementation.
gkd_recovery_power_allowed() {
	[ "$2" = 1 ] && return 0
	case "$1" in ''|*[!0-9]*) return 1 ;; esac
	[ "${#1}" -le 3 ] && [ "$1" -ge 30 ] && [ "$1" -le 100 ]
}

power_gate() {
	config=/run/gkd-config/current/effective.conf
	[ -r "$config" ] || config=/etc/gkd-mini/gdkmini.conf
	probe=$(/usr/sbin/gkd-battery-notify --probe-once "$config" 2>/dev/null || true)
	percent=${probe#percent=}; percent=${percent%% *}
	powered=0
	for state in /sys/class/udc/*/state; do
		[ -r "$state" ] || continue
		case "$(sed -n '1p' "$state")" in
			configured|addressed|powered) powered=1 ;;
		esac
	done
	gkd_recovery_power_allowed "$percent" "$powered"
}
