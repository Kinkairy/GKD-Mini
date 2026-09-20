#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
script=$lane/device/gkd-application-network
root=$(mktemp -d /tmp/gkd-mini-public/gkd-application-network-test.XXXXXX)

cleanup()
{
	for owner in "$root"/state*/ssh.owned; do
		[ -s "$owner" ] || continue
		read -r pid start <"$owner" || true
		case "${pid-}" in ''|*[!0-9]*) ;; *) kill "$pid" 2>/dev/null || true ;; esac
	done
	rm -rf -- "$root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$root/bin" "$root/proc" "$root/p2" "$root/sim"
: >"$root/host-key"

cat >"$root/bin/ip" <<'IP'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$GKD_NETWORK_SIM/commands"
case "$*" in
"-f inet addr show dev usb0") cat "$GKD_NETWORK_SIM/addresses" ;;
"route") cat "$GKD_NETWORK_SIM/routes" ;;
"addr add 192.168.137.2/24 dev usb0")
	[ ! -e "$GKD_NETWORK_SIM/fail-ip-add" ] || exit 1
	printf 'inet 192.168.137.2/24 scope global usb0\n' >>"$GKD_NETWORK_SIM/addresses" ;;
"addr del 192.168.137.2/24 dev usb0")
	grep -Fv 'inet 192.168.137.2/24 scope global usb0' \
		"$GKD_NETWORK_SIM/addresses" >"$GKD_NETWORK_SIM/addresses.new" || true
	mv "$GKD_NETWORK_SIM/addresses.new" "$GKD_NETWORK_SIM/addresses" ;;
"route add default via 192.168.137.1 dev usb0 metric 50")
	[ ! -e "$GKD_NETWORK_SIM/fail-route-add" ] || exit 1
	if [ -e "$GKD_NETWORK_SIM/route-output" ]; then
		cat "$GKD_NETWORK_SIM/route-output" >>"$GKD_NETWORK_SIM/routes"
	else
		printf 'default via 192.168.137.1 dev usb0 metric 50\n' >>"$GKD_NETWORK_SIM/routes"
	fi ;;
"route del default via 192.168.137.1 dev usb0 metric 50")
	grep -Ev '^default[[:space:]]+via[[:space:]]+192\.168\.137\.1[[:space:]]+dev[[:space:]]+usb0[[:space:]]+metric[[:space:]]+50[[:space:]]*$' \
		"$GKD_NETWORK_SIM/routes" >"$GKD_NETWORK_SIM/routes.new" || true
	mv "$GKD_NETWORK_SIM/routes.new" "$GKD_NETWORK_SIM/routes" ;;
*) printf 'unexpected ip command: %s\n' "$*" >&2; exit 2 ;;
esac
IP

cat >"$root/bin/ping" <<'PING'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$GKD_NETWORK_SIM/pings"
[ ! -e "$GKD_NETWORK_SIM/fail-ping" ]
PING

cat >"$root/bin/dropbear" <<'DROPBEAR'
#!/bin/sh
set -eu
[ ! -e "$GKD_NETWORK_SIM/fail-dropbear" ] || exit 1
pid_file=
previous=
for argument in "$@"; do
	[ "$previous" != -P ] || pid_file=$argument
	previous=$argument
done
[ -n "$pid_file" ] || exit 2
pid=$$
process=$GKD_APPLICATION_NETWORK_TEST_PROC_ROOT/$pid
mkdir -p "$process"
printf '%s\n' "$pid" >"$pid_file"
printf '%s\n' 987654 >"$process/start"
printf '%s\n' dropbear >"$process/comm"
ln -s "$GKD_APPLICATION_NETWORK_TEST_DROPBEAR" "$process/exe"
{
	printf '%s\000' "$GKD_APPLICATION_NETWORK_TEST_DROPBEAR"
	for argument in "$@"; do printf '%s\000' "$argument"; done
} >"$process/cmdline"
trap 'rm -rf -- "$process"; exit 0' HUP INT TERM
while :; do sleep 1; done
DROPBEAR
chmod 0755 "$root/bin/ip" "$root/bin/ping" "$root/bin/dropbear"

config=$root/effective.conf
write_config()
{
	cat >"$config" <<'CONFIG'
usb_internet_interface=usb0
usb_internet_device_ip=192.168.137.2
usb_internet_gateway=192.168.137.1
usb_internet_prefix_length=24
usb_internet_poll_seconds=2
usb_internet_health_timeout_seconds=2
usb_internet_enable_timeout_seconds=4
usb_internet_ssh_port=22
usb_internet_ssh_idle_seconds=600
CONFIG
}

reset_case()
{
	case_name=$1
	state=$root/state-$case_name
	sim=$root/sim-$case_name
	p2=$root/p2-$case_name
	mkdir -p "$state" "$sim" "$p2"
	printf 'inet 10.1.1.2/24 scope global usb0\n' >"$sim/addresses"
	: >"$sim/routes"
	: >"$sim/commands"
	: >"$sim/pings"
	write_config
}

run_network()
{
	action=$1
	(
		exec 3<"$p2"
		GKD_APPLICATION_NETWORK_TESTING=1 \
		GKD_APPLICATION_NETWORK_TEST_STATE_ROOT="$state" \
		GKD_APPLICATION_NETWORK_TEST_PROC_ROOT="$root/proc" \
		GKD_APPLICATION_NETWORK_TEST_IP="$root/bin/ip" \
		GKD_APPLICATION_NETWORK_TEST_PING="$root/bin/ping" \
		GKD_APPLICATION_NETWORK_TEST_DROPBEAR="$root/bin/dropbear" \
		GKD_APPLICATION_NETWORK_TEST_HOST_KEY="$root/host-key" \
		GKD_NETWORK_SIM="$sim" \
		export GKD_APPLICATION_NETWORK_TESTING \
			GKD_APPLICATION_NETWORK_TEST_STATE_ROOT \
			GKD_APPLICATION_NETWORK_TEST_PROC_ROOT \
			GKD_APPLICATION_NETWORK_TEST_IP GKD_APPLICATION_NETWORK_TEST_PING \
			GKD_APPLICATION_NETWORK_TEST_DROPBEAR GKD_APPLICATION_NETWORK_TEST_HOST_KEY \
			GKD_NETWORK_SIM
		sh "$script" "$action" 3 "$config"
	)
}

reset_case present
printf 'search example.invalid\nnameserver 10.1.1.1\n' >"$p2/resolv.conf"
cp "$p2/resolv.conf" "$root/original-resolv"
run_network start >"$root/start.out"
grep -Fq 'GKD_APPLICATION_NETWORK=online ip=192.168.137.2/24 gateway=192.168.137.1 ssh=192.168.137.2:22 poll=2' "$root/start.out"
grep -Fq 'inet 10.1.1.2/24 scope global usb0' "$sim/addresses"
grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ "$(cat "$p2/resolv.conf")" = 'nameserver 192.168.137.1' ]
cmp -s "$root/original-resolv" "$state/resolv.backup"
[ -s "$state/ssh.owned" ]
run_network poll | grep -Fq 'GKD_APPLICATION_NETWORK=online'
run_network status | grep -Fq 'GKD_APPLICATION_NETWORK=online'
: >"$sim/fail-ping"
if run_network poll >"$root/degraded-poll.out"; then exit 1; fi
grep -Fq 'reason=owned-state-unhealthy' "$root/degraded-poll.out"
[ "$(cat "$p2/resolv.conf")" = 'nameserver 192.168.137.1' ]
[ -e "$state/ip.owned" ] && [ -e "$state/route.owned" ] && [ -s "$state/ssh.owned" ]
rm "$sim/fail-ping"
run_network poll | grep -Fq 'GKD_APPLICATION_NETWORK=online'
run_network stop | grep -Fq 'GKD_APPLICATION_NETWORK=stopped'
cmp -s "$root/original-resolv" "$p2/resolv.conf"
[ ! -e "$state/resolv.backup" ] && [ ! -e "$state/ip.owned" ] &&
	[ ! -e "$state/route.owned" ] && [ ! -e "$state/ssh.owned" ]
grep -Fq 'inet 10.1.1.2/24 scope global usb0' "$sim/addresses"
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
! grep -Fq 'addr del 10.1.1.2' "$sim/commands"

reset_case absent
run_network start >/dev/null
[ -f "$p2/resolv.conf" ]
run_network stop >/dev/null
[ ! -e "$p2/resolv.conf" ]

reset_case edited
printf 'nameserver 10.1.1.1\n' >"$p2/resolv.conf"
run_network start >/dev/null
printf 'nameserver 9.9.9.9\n' >"$p2/resolv.conf"
if run_network stop >"$root/edited-stop.out"; then exit 1; fi
grep -Fq 'GKD_APPLICATION_NETWORK=blocked reason=dns-modified' "$root/edited-stop.out"
[ "$(cat "$p2/resolv.conf")" = 'nameserver 9.9.9.9' ]
[ -f "$state/resolv.backup" ] && [ -f "$state/dns.state" ]
run_network status | grep -Fq 'GKD_APPLICATION_NETWORK=diagnosed reason=dns-modified'
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$state/ssh.owned" ]

reset_case ping
printf 'nameserver 10.1.1.1\n' >"$p2/resolv.conf"
: >"$sim/fail-ping"
if run_network start >"$root/ping.out"; then exit 1; fi
grep -Fq 'reason=ics-unavailable-or-state-conflict' "$root/ping.out"
[ "$(cat "$p2/resolv.conf")" = 'nameserver 10.1.1.1' ]
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]
rm "$sim/fail-ping"
run_network poll | grep -Fq 'GKD_APPLICATION_NETWORK=online'
run_network stop >/dev/null

reset_case busybox_spacing
printf 'default via 192.168.137.1 dev usb0  metric 50 \n' >"$sim/route-output"
: >"$sim/fail-ping"
if run_network start >"$root/busybox-spacing.out"; then exit 1; fi
grep -Fq 'reason=ics-unavailable-or-state-conflict' "$root/busybox-spacing.out"
[ ! -s "$sim/routes" ]
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"

reset_case busybox_tabs
printf 'default\tvia\t192.168.137.1\tdev\tusb0\tmetric\t50\n' >"$sim/route-output"
: >"$sim/fail-ping"
if run_network start >"$root/busybox-tabs.out"; then exit 1; fi
grep -Fq 'reason=ics-unavailable-or-state-conflict' "$root/busybox-tabs.out"
[ ! -s "$sim/routes" ]
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"

reset_case single_spacing
run_network start >/dev/null
grep -Fxq 'default via 192.168.137.1 dev usb0 metric 50' "$sim/routes"
run_network stop >/dev/null
[ ! -s "$sim/routes" ]

reset_case sshfail
printf 'nameserver 10.1.1.1\n' >"$p2/resolv.conf"
: >"$sim/fail-dropbear"
if run_network start >"$root/sshfail.out"; then exit 1; fi
[ "$(cat "$p2/resolv.conf")" = 'nameserver 10.1.1.1' ]
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]

reset_case ipfail
: >"$sim/fail-ip-add"
if run_network start >"$root/ipfail.out"; then exit 1; fi
grep -Fq 'inet 10.1.1.2/24 scope global usb0' "$sim/addresses"
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]

reset_case routefail
: >"$sim/fail-route-add"
if run_network start >"$root/routefail.out"; then exit 1; fi
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]

reset_case dnslink
printf 'foreign\n' >"$root/foreign-resolv"
ln -s "$root/foreign-resolv" "$p2/resolv.conf"
if run_network start >"$root/dnslink.out"; then exit 1; fi
[ -L "$p2/resolv.conf" ] && [ "$(cat "$root/foreign-resolv")" = foreign ]
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]

reset_case nokey
saved_key=$root/host-key.saved
mv "$root/host-key" "$saved_key"
if run_network start >"$root/nokey.out"; then exit 1; fi
mv "$saved_key" "$root/host-key"
! grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
[ ! -s "$sim/routes" ]

reset_case foreign_ip
printf 'inet 192.168.137.2/24 scope global usb0\n' >>"$sim/addresses"
if run_network start >"$root/foreign-ip.out"; then exit 1; fi
grep -Fq 'inet 192.168.137.2/24 scope global usb0' "$sim/addresses"
! grep -Fq 'addr del 192.168.137.2/24' "$sim/commands"

reset_case foreign_route
printf 'default via 172.16.0.1 dev eth0 metric 20\n' >"$sim/routes"
if run_network start >"$root/foreign-route.out"; then exit 1; fi
grep -Fq 'default via 172.16.0.1 dev eth0 metric 20' "$sim/routes"
! grep -Fq 'route del default via 172.16.0.1' "$sim/commands"

for foreign_route in \
	'default via 192.168.137.254 dev usb0 metric 50' \
	'default via 192.168.137.1 dev eth0 metric 50' \
	'default via 192.168.137.1 dev usb0 metric 51' \
	'default via 192.168.137.1 dev usb0 metric 50 extra'; do
	reset_case foreign_owned_route
	printf '%s\n' "$foreign_route" >"$sim/routes"
	: >"$state/route.owned"
	run_network stop >/dev/null
	grep -Fxq "$foreign_route" "$sim/routes"
	! grep -Fq 'route del default via 192.168.137.1 dev usb0 metric 50' "$sim/commands"
done

reset_case foreign_ssh
sleep 60 &
foreign_pid=$!
mkdir -p "$root/proc/$foreign_pid"
printf '222333\n' >"$root/proc/$foreign_pid/start"
printf 'dropbear\n' >"$root/proc/$foreign_pid/comm"
ln -s "$root/bin/dropbear" "$root/proc/$foreign_pid/exe"
printf '%s\000-F\000-r\000%s\000-P\000%s\000-p\00010.1.1.2:22\000-I\000600\000' \
	"$root/bin/dropbear" "$root/host-key" "$state/dropbear.pid" \
	>"$root/proc/$foreign_pid/cmdline"
printf '%s %s\n' "$foreign_pid" 222333 >"$state/ssh.owned"
run_network stop >/dev/null
kill -0 "$foreign_pid"
kill "$foreign_pid"
wait "$foreign_pid" 2>/dev/null || true

reset_case config
printf 'usb_internet_ssh_port=23\n' >>"$config"
if run_network status >"$root/config.out"; then exit 1; fi
grep -Fq 'GKD_APPLICATION_NETWORK=blocked reason=invalid-config' "$root/config.out"

if grep -E 'udhc|dhcp|gkd_usb_menu|gkd_network_status|usb-chooser|config\.sh' "$script"; then
	echo GKD_APPLICATION_NETWORK_FIXTURE=BLOCKED legacy-source >&2
	exit 1
fi

printf '%s\n' 'GKD_APPLICATION_NETWORK_FIXTURE=PASS start/poll/retry/status/stop=1 dns-present/absent/edit/symlink=1 ip/route/ping/ssh/key-cleanup=1 route-whitespace/strict-ownership=1 foreign-ip/route/listener-preserved=1 management-preserved=1 config-strict=1'
