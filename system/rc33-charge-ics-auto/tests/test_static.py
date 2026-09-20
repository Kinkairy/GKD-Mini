from pathlib import Path

root = Path(__file__).resolve().parents[1]
daemon = (root / "device/gkd-usb-internet-auto").read_text(encoding="utf-8")
init = (root / "device/S97gkd-usb-internet-auto").read_text(encoding="utf-8")

for token in (
    "event=mode-selected*detail=charge",
    "event=chooser-visible-grabbed",
    "event=usb-reattached",
    "event=fail-open",
    "gkd_config_ipv4 usb_internet_device_ip 192.168.137.2",
    '"$internet_ip/$internet_prefix"',
    '"default via $gateway dev $iface"',
    "usb_internet_gateway 192.168.137.1",
    'ping -c 1 -W "$enable_timeout" "$gateway"',
    "ics_ready",
    'ping -c 1 -W "$health_timeout" "$gateway"',
    "restore_dns",
    'wc -c <"$trace"',
    "/usr/local/etc/gkd-mini/usb-internet-resolv.state",
    "network_ui=/sys/class/graphics/fb0/gkd_network_status",
    "menu_ui=/sys/class/graphics/fb0/gkd_usb_menu",
    "usb_internet_panel_lease_ms 5000",
    "usb_internet_poll_seconds 2",
    'sleep "$poll_seconds"',
    "renew_network_panel",
    "status charge %s",
    "set_network_ui offline",
    "set_network_ui online",
    "explicit_ssh_pid=$run/explicit-ssh.pid",
    "explicit_ssh_native_pid=$run/explicit-ssh.native.pid",
    "start_explicit_ssh",
    "stop_explicit_ssh",
    'usb_internet_ssh_port 22 1 65535',
    'usb_internet_ssh_idle_seconds 600 60 86400',
    '-p "$internet_ip:$explicit_ssh_port"',
    "explicit_ssh_process_matches",
    "enabled-explicit-ssh",
    "enabled-network-only",
    "online %s",
):
    assert token in daemon, token

assert 'if [ "$1" = online ]; then' in daemon
assert 'awk -v wanted="$internet_ip/$internet_prefix"' in daemon
assert 'printf \'online %s\\n\' "$display_ip"' in daemon
assert 'if [ "$1" = online ] && explicit_ssh_alive' not in daemon


assert "printf 'online\\n'" not in daemon

for forbidden in ("powershell", "New-NetNat", "EnableSharing", "system(", "stat -c"):
    assert forbidden not in daemon

for external_probe in ("nslookup", "www.microsoft.com", '"$probe_ip"', "usb_internet_probe_ip"):
    assert external_probe not in daemon

# The explicit physical-USB channel intentionally accepts the existing root
# password and key authentication.  Blank-password mode is never enabled.
dropbear_start = daemon.split("/usr/sbin/dropbear -F", 1)[1].split("&", 1)[0]
for forbidden_auth in (' -B ', ' -s ', ' -g '):
    assert forbidden_auth not in dropbear_start
assert daemon.index("stop_explicit_ssh") < daemon.index('ip addr del "$internet_ip/$internet_prefix"')

assert '"$service" cleanup' in init
assert '"$service" daemon' in init
assert "kill -9" in init
assert daemon.index("if enable_once; then") < daemon.index("renew_network_panel", daemon.index("if enable_once; then"))
print("RC33_CHARGE_ICS_STATIC=PASS")
