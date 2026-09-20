#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT.parent / "rc33-charge-ics-auto/device/gkd-usb-internet-auto"
CONFIG_LIB = ROOT / "device/config.sh"


def run(config: str):
    with tempfile.TemporaryDirectory(prefix="gkd-network-config-") as directory:
        path = Path(directory) / "gdkmini.conf"
        path.write_text(config, encoding="ascii")
        env = dict(os.environ)
        env["GKD_CONFIG_FILE"] = str(path)
        env["GKD_CONFIG_LIB"] = str(CONFIG_LIB)
        result = subprocess.run(
            ["sh", str(SCRIPT), "config"], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            check=False,
        )
        return result.returncode, result.stdout


def test_defaults_and_custom_values():
    code, output = run("usb_device_ip=10.1.1.2\n")
    assert code == 0
    assert "management:10.1.1.2" in output
    assert "internet:192.168.137.2/24" in output
    assert "gateway:192.168.137.1 poll:2s" in output
    assert "ssh:22 idle:600s" in output
    code, output = run(
        "usb_device_ip=10.8.0.2\n"
        "usb_internet_interface=usb1\n"
        "usb_internet_device_ip=172.20.10.2\n"
        "usb_internet_gateway=172.20.10.1\n"
        "usb_internet_prefix_length=28\n"
        "usb_internet_poll_seconds=3\n"
        "usb_internet_panel_lease_ms=5000\n"
        "usb_internet_ssh_port=2222\n"
        "usb_internet_ssh_idle_seconds=900\n"
    )
    assert code == 0
    assert "iface:usb1 management:10.8.0.2 internet:172.20.10.2/28" in output
    assert "gateway:172.20.10.1 poll:3s" in output
    assert "recheck:2s initial:4s lease:5000ms" in output
    assert "ssh:2222 idle:900s" in output


def test_invalid_values_fail_closed():
    for config in (
        "usb_internet_device_ip=192.168.001.2\n",
        "usb_internet_gateway=999.1.1.1\n",
        "usb_internet_prefix_length=31\n",
        "usb_internet_poll_seconds=0\n",
        "usb_internet_poll_seconds=5\nusb_internet_panel_lease_ms=5000\n",
        "usb_internet_interface=usb 0\n",
        "usb_internet_ssh_port=0\n",
        "usb_internet_ssh_port=65536\n",
        "usb_internet_ssh_idle_seconds=59\n",
        "usb_internet_gateway=192.168.137.1\nusb_internet_gateway=192.168.1.1\n",
    ):
        code, output = run(config)
        assert code == 65
        assert output == ""


if __name__ == "__main__":
    test_defaults_and_custom_values()
    test_invalid_values_fail_closed()
    print("RC33_NETWORK_CONFIG=PASS")
