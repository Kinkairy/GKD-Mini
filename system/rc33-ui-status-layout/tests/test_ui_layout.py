#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "device/gkd-ui-layout"
CONFIG_LIB = ROOT / "device/config.sh"
FRAGMENT = ROOT / "source/gdkmini.conf.fragment"
PROJECT = ROOT.parents[1]
PATCH = PROJECT / "kernel/current/patches/0001-rc34-accepted-kernel.patch"


def run(config: str):
    with tempfile.TemporaryDirectory(prefix="gkd-ui-layout-") as directory:
        root = Path(directory)
        config_path = root / "gdkmini.conf"
        sysfs_path = root / "gkd_usb_menu"
        config_path.write_text(config, encoding="utf-8")
        sysfs_path.write_text("", encoding="ascii")
        env = dict(os.environ)
        env["GKD_UI_LAYOUT_CONFIG"] = str(config_path)
        env["GKD_UI_LAYOUT_SYSFS"] = str(sysfs_path)
        env["GKD_CONFIG_LIB"] = str(CONFIG_LIB)
        result = subprocess.run(
            ["sh", str(SCRIPT), "apply"], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            check=False,
        )
        return result.returncode, sysfs_path.read_text(encoding="ascii")


def test_defaults_and_configured_values():
    code, output = run("ui_language=zh\n")
    assert code == 0
    assert output == "layout 34 211 15 210 13 11\n"
    code, output = run(
        "ui_status_text_x=35\nui_status_text_y=212\n"
        "ui_status_icon_x=14\nui_status_icon_y=209\n"
        "ui_status_icon_width=14\nui_status_icon_height=12\n"
    )
    assert code == 0
    assert output == "layout 35 212 14 209 14 12\n"


def test_invalid_values_fail_closed():
    for config in (
        "ui_status_text_y=219\n",
        "ui_status_icon_width=0\n",
        "ui_status_icon_x=90\nui_status_icon_width=13\n",
        "ui_status_icon_height=011\n",
        "ui_status_text_x=34\nui_status_text_x=35\n",
    ):
        code, output = run(config)
        assert code == 65
        assert output == ""


def test_kernel_contract_and_config_fragment():
    text = PATCH.read_text(encoding="utf-8")
    fragment = FRAGMENT.read_text(encoding="utf-8")
    for token in (
        "x1830_status_layout_parse",
        "status_text_x",
        "status_text_y",
        "status_icon_width",
        "status_icon_height",
        '"layout ", 7',
        "X1830_STATUS_TEXT_Y_DEFAULT 211U",
        "908aad72c5cf20b1db46e228752a1aab658b18591d61932c6ccf9a00257661b3",
    ):
        assert token in text
    for key in (
        "ui_status_text_x=34", "ui_status_text_y=211",
        "ui_status_icon_x=15", "ui_status_icon_y=210",
        "ui_status_icon_width=13", "ui_status_icon_height=11",
        "usb_internet_interface=usb0",
        "usb_internet_device_ip=192.168.137.2",
        "usb_internet_gateway=192.168.137.1",
        "usb_internet_prefix_length=24",
        "usb_internet_poll_seconds=2",
        "usb_internet_panel_lease_ms=5000",
    ):
        assert key in fragment


if __name__ == "__main__":
    test_defaults_and_configured_values()
    test_invalid_values_fail_closed()
    test_kernel_contract_and_config_fragment()
    print("RC33_UI_STATUS_LAYOUT=PASS")
