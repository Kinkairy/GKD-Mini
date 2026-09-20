#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
script=$lane/device/gkd-application-usb
root=$(mktemp -d /tmp/gkd-mini-public/gkd-application-usb-test.XXXXXX)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/configfs/usb_gadget" "$root/udc/dwc2" "$root/proc/1" "$root/sys/block"
: >"$root/proc/1/mountinfo"
printf 'Filename Type Size Used Priority\n' >"$root/proc/swaps"
gadget=$root/configfs/usb_gadget/gkd_recovery

bootstrap()
{
	rm -rf -- "$gadget"
	mkdir -p "$gadget/strings/0x409" "$gadget/configs/c.1/strings/0x409" 		"$gadget/functions/rndis.usb0/os_desc/interface.rndis" "$gadget/os_desc"
	printf '%s' 0x0525 >"$gadget/idVendor"
	printf '%s' 0xa4a2 >"$gadget/idProduct"
	printf '%s' 0xef >"$gadget/bDeviceClass"
	printf '%s' 0x02 >"$gadget/bDeviceSubClass"
	printf '%s' 0x01 >"$gadget/bDeviceProtocol"
	printf '%s' GKDROUND64R5 >"$gadget/strings/0x409/serialnumber"
	printf '%s' 'GKD Mini' >"$gadget/strings/0x409/manufacturer"
	printf '%s' 'GKD Mini Recovery' >"$gadget/strings/0x409/product"
	printf '%s' Recovery >"$gadget/configs/c.1/strings/0x409/configuration"
	printf '%s' 120 >"$gadget/configs/c.1/MaxPower"
	printf '%s' ef >"$gadget/functions/rndis.usb0/class"
	printf '%s' 04 >"$gadget/functions/rndis.usb0/subclass"
	printf '%s' 01 >"$gadget/functions/rndis.usb0/protocol"
	printf '%s' 02:00:00:00:64:01 >"$gadget/functions/rndis.usb0/dev_addr"
	printf '%s' 02:00:00:00:64:02 >"$gadget/functions/rndis.usb0/host_addr"
	printf 'RNDIS\000\000\000' >"$gadget/functions/rndis.usb0/os_desc/interface.rndis/compatible_id"
	printf '5162001\000' >"$gadget/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id"
	printf '%s' 1 >"$gadget/os_desc/use"
	printf '%s' 0xffffffcd >"$gadget/os_desc/b_vendor_code"
	printf '%s' MSFT100 >"$gadget/os_desc/qw_sign"
	ln -s "../../../../usb_gadget/gkd_recovery/functions/rndis.usb0" "$gadget/configs/c.1/rndis.usb0"
	ln -s "../../../usb_gadget/gkd_recovery/configs/c.1" "$gadget/os_desc/c.1"
	printf '%s' dwc2 >"$gadget/UDC"
}

# This fixture controls the native gate's exit status and records the exact
# twice-checked target. Card actual device identity is covered in card_guard_fixture.c.
cat >"$root/guard" <<'GUARD'
#!/bin/sh
printf '%s\n' "$1" >>"$GKD_APPLICATION_USB_TEST_STATE_ROOT/guard.log"
[ ! -e "$GKD_APPLICATION_USB_TEST_STATE_ROOT/guard-block" ]
GUARD
chmod 755 "$root/guard"

run_usb()
{
	GKD_APPLICATION_USB_TEST_GUARD="$root/guard" 	GKD_APPLICATION_USB_TESTING=1 	GKD_APPLICATION_USB_TEST_CONFIGFS="$root/configfs" 	GKD_APPLICATION_USB_TEST_UDC_ROOT="$root/udc" 	GKD_APPLICATION_USB_TEST_PROC_ROOT="$root/proc" 	GKD_APPLICATION_USB_TEST_SYS_ROOT="$root/sys" 	GKD_APPLICATION_USB_TEST_STATE_ROOT="$root/state" 	GKD_APPLICATION_USB_TEST_FAULT="${2-}" 		sh "$script" "$1"
}

assert_network()
{
	[ "$(cat "$gadget/UDC")" = dwc2 ]
	[ -L "$gadget/configs/c.1/rndis.usb0" ]
	[ -L "$gadget/configs/c.1/acm.GKDROUND64R5" ]
	[ ! -L "$gadget/configs/c.1/mass_storage.0" ]
	[ "$(cat "$gadget/functions/rndis.usb0/dev_addr")" = 02:00:00:00:64:01 ]
	[ "$(cat "$gadget/functions/rndis.usb0/host_addr")" = 02:00:00:00:64:02 ]
}

expect_blocked()
{
	mode=$1 fault=$2 reason=$3
	if run_usb "$mode" "$fault" >"$root/$fault.out" 2>"$root/$fault.err"; then
		echo "fault $fault unexpectedly passed" >&2
		exit 1
	fi
	grep -Fq "reason=$reason" "$root/$fault.err"
	run_usb status | grep -Fq "DIAGNOSED reason=$reason"
}

bootstrap
run_usb network >"$root/network.out" 2>"$root/network.err" ||
	{ cat "$root/network.err" >&2; exit 1; }
grep -Fq 'PASS mode=network serial=GKDROUND64R5 lun=none' "$root/network.out"
assert_network
# Real configfs returns relative links, including functions created by A.
rm "$gadget/configs/c.1/acm.GKDROUND64R5"
ln -s "../../../../usb_gadget/gkd_recovery/functions/acm.GKDROUND64R5" "$gadget/configs/c.1/acm.GKDROUND64R5"
run_usb status | grep -Fq 'ACTIVE mode=network'
# Both exact representations of the same char are valid, not arbitrary low bytes.
printf '%s' 0xcd >"$gadget/os_desc/b_vendor_code"
run_usb status | grep -Fq 'ACTIVE mode=network'
printf '%s' 0xffffffcd >"$gadget/os_desc/b_vendor_code"
for invalid in 0x00 0xce 0x123456cd 0x1000000cd malformed; do
	printf '%s' "$invalid" >"$gadget/os_desc/b_vendor_code"
	if run_usb network >"$root/vendor.out" 2>"$root/vendor.err"; then exit 1; fi
	grep -Fq 'reason=foreign-or-uncontrolled-gadget' "$root/vendor.err"
	[ "$(cat "$gadget/UDC")" = dwc2 ]
done
printf '%s' 0xffffffcd >"$gadget/os_desc/b_vendor_code"
run_usb network >/dev/null
for invalid in "../../../../usb_gadget/foreign/functions/rndis.usb0" "../../../../usb_gadget/gkd_recovery/functions/missing" "rndis.usb0"; do
	rm "$gadget/configs/c.1/rndis.usb0"
	ln -s "$invalid" "$gadget/configs/c.1/rndis.usb0"
	if run_usb network >"$root/link.out" 2>"$root/link.err"; then exit 1; fi
	grep -Fq 'reason=foreign-or-uncontrolled-gadget' "$root/link.err"
	[ "$(cat "$gadget/UDC")" = dwc2 ]
done
rm "$gadget/configs/c.1/rndis.usb0"
ln -s "../../../../usb_gadget/gkd_recovery/functions/rndis.usb0" "$gadget/configs/c.1/rndis.usb0"
run_usb network >/dev/null
printf 'preserved' >"$gadget/functions/rndis.usb0/identity-marker"
run_usb status | grep -Fq 'ACTIVE mode=network serial=GKDROUND64R5 lun=none'

: >"$root/state/guard.log"
run_usb storage | grep -Fq 'PASS mode=storage serial=GKDROUND64R5 lun=/dev/mmcblk1'
[ "$(wc -l <"$root/state/guard.log")" -eq 2 ]
[ "$(sort -u "$root/state/guard.log")" = mmcblk1 ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/file")" = /dev/mmcblk1 ]
rm "$gadget/configs/c.1/mass_storage.0"
ln -s "../../../../usb_gadget/gkd_recovery/functions/mass_storage.0" "$gadget/configs/c.1/mass_storage.0"
run_usb status | grep -Fq 'ACTIVE mode=storage'
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/ro")" = 0 ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/removable")" = 1 ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/nofua")" = 0 ]

run_usb eject >"$root/eject.out" 2>"$root/eject.err" ||
	{ cat "$root/eject.err" >&2; exit 1; }
grep -Fq 'PASS mode=network serial=GKDROUND64R5 lun=none' "$root/eject.out"
assert_network
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/forced_eject")" = 1 ]
[ "$(wc -c <"$gadget/functions/mass_storage.0/lun.0/file")" -eq 1 ]
grep -Fxq /dev/mmcblk1 "$root/state/flush-log"
[ "$(cat "$gadget/functions/rndis.usb0/identity-marker")" = preserved ]

: >"$root/state/guard.log"
run_usb debug | grep -Fq 'PASS mode=debug serial=GKDROUND64R5 lun=/dev/mmcblk0'
[ "$(wc -l <"$root/state/guard.log")" -eq 2 ]
[ "$(sort -u "$root/state/guard.log")" = mmcblk0 ]
run_usb status | grep -Fq 'ACTIVE mode=debug serial=GKDROUND64R5 lun=/dev/mmcblk0'
run_usb eject >/dev/null
assert_network
grep -Fxq /dev/mmcblk0 "$root/state/flush-log"

# Failed preflight leaves the bound RNDIS instance untouched.
: >"$root/state/guard-block"
if run_usb storage >"$root/mounted.out" 2>"$root/mounted.err"; then exit 1; fi
grep -Fq 'reason=media-busy-or-unreadable' "$root/mounted.err"
assert_network
[ "$(cat "$gadget/functions/rndis.usb0/identity-marker")" = preserved ]
rm "$root/state/guard-block"
run_usb network >/dev/null

# UDC clear must be read back before any function mutation.
expect_blocked storage unbind-readback safe-unbind
[ "$(cat "$gadget/UDC")" = dwc2 ]
[ ! -L "$gadget/configs/c.1/mass_storage.0" ]
[ "$(cat "$gadget/functions/rndis.usb0/identity-marker")" = preserved ]
run_usb network >/dev/null
assert_network

# A failure after confirmed unbind remains explicitly diagnosed and unbound.
expect_blocked storage post-unbind-fail prepare-functions
[ -z "$(cat "$gadget/UDC")" ]
[ ! -L "$gadget/configs/c.1/mass_storage.0" ]
[ "$(cat "$gadget/functions/rndis.usb0/identity-marker")" = preserved ]
run_usb network >/dev/null
assert_network

# LUN store drift is detected before linking or rebinding the gadget.
expect_blocked storage add-lun-readback attach-lun
[ -z "$(cat "$gadget/UDC")" ]
[ -z "$(cat "$gadget/functions/mass_storage.0/lun.0/file")" ]
[ ! -L "$gadget/configs/c.1/mass_storage.0" ]
run_usb network >/dev/null
assert_network

# Bind readback failure holds the prepared LUN unbound until explicit eject.
expect_blocked storage bind-readback bind-gadget
[ -z "$(cat "$gadget/UDC")" ]
[ -L "$gadget/configs/c.1/mass_storage.0" ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/file")" = /dev/mmcblk1 ]
run_usb eject >/dev/null
assert_network

# A failed post-clear flush keeps a durable retry obligation and stays diagnosed.
run_usb storage >/dev/null
expect_blocked eject flush-fail safe-unbind
[ "$(cat "$gadget/UDC")" = dwc2 ]
[ -z "$(cat "$gadget/functions/mass_storage.0/lun.0/file")" ]
[ "$(cat "$root/state/pending-flush")" = /dev/mmcblk1 ]
run_usb eject >/dev/null
[ ! -e "$root/state/pending-flush" ]
assert_network

# Base identity drift is rejected before unbind; no gadget is recreated.
printf '%s' 02:00:00:00:64:99 >"$gadget/functions/rndis.usb0/dev_addr"
run_usb status | grep -Fq 'DIAGNOSED reason=foreign-or-uncontrolled-gadget'
if run_usb network >"$root/mac-drift.out" 2>"$root/mac-drift.err"; then exit 1; fi
grep -Fq 'reason=foreign-or-uncontrolled-gadget' "$root/mac-drift.err"
[ "$(cat "$gadget/UDC")" = dwc2 ]
[ "$(cat "$gadget/functions/rndis.usb0/identity-marker")" = preserved ]
printf '%s' 02:00:00:00:64:01 >"$gadget/functions/rndis.usb0/dev_addr"
run_usb network >/dev/null
assert_network

mkdir "$root/state/card-operation.lock"
if run_usb storage >"$root/lock.out" 2>"$root/lock.err"; then exit 1; fi
grep -Fq 'reason=operation-busy' "$root/lock.err"
rmdir "$root/state/card-operation.lock"

bootstrap
mkdir "$root/configfs/usb_gadget/foreign"
if run_usb network >"$root/foreign.out" 2>"$root/foreign.err"; then exit 1; fi
grep -Fq 'reason=foreign-or-uncontrolled-gadget' "$root/foreign.err"
[ -d "$root/configfs/usb_gadget/foreign" ]

printf '%s\n' 'GKD_APPLICATION_USB_FIXTURE=PASS network=1 storage=1 debug=1 eject=1 guard-twice=1 forced-eject=1 flush=1 rndis-preserved=1 identity-readback=1 configfs-relative-links=1 signed-char-vendor=1 foreign-dangling-loop-rejected=1 unbind-readback=1 post-unbind-fail=1 lun-readback=1 bind-readback=1 flush-retry=1 operation-lock=1 foreign=preserved'
