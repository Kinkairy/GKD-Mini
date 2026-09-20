#!/bin/sh
set -eu

lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
script=$lane/../../kernel/current/initramfs/gkd-recovery-usb
root=$(mktemp -d /tmp/gkd-mini-public/gkd-recovery-usb-test.XXXXXX)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM

mkdir -p "$root/configfs/usb_gadget" "$root/udc/dwc2"
: >"$root/device"
: >"$root/mounts"
cat >"$root/ssh-hook" <<'EOF'
#!/bin/sh
set -eu
[ "${ROUND64_RESCUE-}" = p1-root-offline ]
[ "${ROUND64_SSH_IDENTITY_DIR-}" = /run/gkd-round64-ssh ]
printf '%s\n' "$1" >>"${GKD_RECOVERY_USB_TEST_ROOT:?}/ssh-actions"
EOF
chmod 0755 "$root/ssh-hook"

run_usb()
{
	GKD_RECOVERY_USB_TESTING=1 \
	GKD_RECOVERY_USB_TEST_ROOT="$root" \
	GKD_RECOVERY_USB_CONFIGFS="$root/configfs" \
	GKD_RECOVERY_USB_UDC_ROOT="$root/udc" \
	GKD_RECOVERY_USB_MOUNTS="$root/mounts" \
	GKD_RECOVERY_USB_SSH_HOOK="$root/ssh-hook" \
	GKD_RECOVERY_USB_DEVICE="$root/device" \
		"$script" "$1"
}

gadget=$root/configfs/usb_gadget/gkd_recovery
assert_rndis_identity()
{
	[ "$(cat "$gadget/functions/rndis.usb0/class")" = ef ]
	[ "$(cat "$gadget/functions/rndis.usb0/subclass")" = 04 ]
	[ "$(cat "$gadget/functions/rndis.usb0/protocol")" = 01 ]
}
run_usb network-start | grep -Fq 'mode=network ssh=10.1.1.2 lun=none'
assert_rndis_identity
[ -L "$gadget/configs/c.1/rndis.usb0" ]
[ ! -e "$gadget/functions/mass_storage.0" ]
[ "$(cat "$gadget/UDC")" = dwc2 ]
[ "$(cat "$gadget/strings/0x409/serialnumber")" = GKDROUND64R5 ]
[ "$(cat "$gadget/functions/rndis.usb0/os_desc/interface.rndis/compatible_id")" = RNDIS ]
[ "$(cat "$gadget/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id")" = 5162001 ]
run_usb status | grep -Fq 'ACTIVE mode=network ssh=10.1.1.2 lun=none'

# A live update lock must block export without touching network ownership.
mkdir "$root/operations/card-operation.lock"
if run_usb export-start >"$root/locked.out" 2>"$root/locked.err"; then exit 1; fi
grep -Fq 'reason=operation-busy' "$root/locked.err"
[ -L "$gadget/configs/c.1/rndis.usb0" ]
[ ! -e "$gadget/functions/mass_storage.0" ]
rmdir "$root/operations/card-operation.lock"

run_usb export-start | grep -Fq 'mode=debug ssh=10.1.1.2 card=/dev/mmcblk0'
assert_rndis_identity
[ -L "$gadget/configs/c.1/rndis.usb0" ]
[ -L "$gadget/configs/c.1/mass_storage.0" ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/file")" = "$root/device" ]
[ "$(cat "$gadget/functions/mass_storage.0/lun.0/ro")" = 0 ]
run_usb status | grep -Fq 'ACTIVE mode=debug ssh=10.1.1.2 card=/dev/mmcblk0'

run_usb export-stop | grep -Fq 'mode=network ssh=10.1.1.2 lun=none'
assert_rndis_identity
[ -L "$gadget/configs/c.1/rndis.usb0" ]
[ ! -e "$gadget/functions/mass_storage.0" ]

printf '%s %s ext4 rw 0 0\n' "$root/device" "$root/system" >"$root/mounts"
if run_usb export-start >"$root/mounted.out" 2>"$root/mounted.err"; then
	echo 'mounted system card was exported' >&2
	exit 1
fi
grep -Fq 'reason=system-card-mounted' "$root/mounted.err"
[ -L "$gadget/configs/c.1/rndis.usb0" ]
[ ! -e "$gadget/functions/mass_storage.0" ]
: >"$root/mounts"

run_usb stop | grep -Fq 'mode=stopped'
[ ! -e "$gadget" ]
mkdir "$root/configfs/usb_gadget/foreign"
if run_usb network-start >"$root/foreign.out" 2>"$root/foreign.err"; then
	echo 'foreign gadget was accepted' >&2
	exit 1
fi
grep -Fq 'reason=network-start' "$root/foreign.err"
[ -d "$root/configfs/usb_gadget/foreign" ]

grep -Fq 'start' "$root/ssh-actions"
grep -Fq 'stop' "$root/ssh-actions"
printf '%s\n' 'GKD_RECOVERY_USB_TEST=PASS network=1 debug=1 return=1 foreign=preserved'
