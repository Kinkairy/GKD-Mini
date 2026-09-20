#!/usr/bin/env python3
"""Derive and verify the bounded configuration delta for each kernel profile."""
import argparse
import json
from pathlib import Path
import re

MODES = ("normal", "dedicated-recovery", "application-minimal")
QUALITY = "rng_core.default_quality=1024"
APPLICATION_DEPENDENCY_DEFAULTS = frozenset({
    "CONFIG_BATTERY_BQ27XXX",
    "CONFIG_BATTERY_CW2015",
    "CONFIG_BATTERY_DS2780",
    "CONFIG_BATTERY_DS2781",
    "CONFIG_BATTERY_DS2782",
    "CONFIG_BATTERY_GAUGE_LTC2941",
    "CONFIG_BATTERY_GOLDFISH",
    "CONFIG_BATTERY_MAX17040",
    "CONFIG_BATTERY_MAX17042",
    "CONFIG_BATTERY_RT5033",
    "CONFIG_BATTERY_SAMSUNG_SDI",
    "CONFIG_BATTERY_SBS",
    "CONFIG_BATTERY_UG3105",
    "CONFIG_CHARGER_ADP5061",
    "CONFIG_CHARGER_BD99954",
    "CONFIG_CHARGER_BQ2415X",
    "CONFIG_CHARGER_BQ24190",
    "CONFIG_CHARGER_BQ24257",
    "CONFIG_CHARGER_BQ24735",
    "CONFIG_CHARGER_BQ2515X",
    "CONFIG_CHARGER_BQ256XX",
    "CONFIG_CHARGER_BQ25890",
    "CONFIG_CHARGER_BQ25980",
    "CONFIG_CHARGER_DETECTOR_MAX14656",
    "CONFIG_CHARGER_GPIO",
    "CONFIG_CHARGER_ISP1704",
    "CONFIG_CHARGER_LP8727",
    "CONFIG_CHARGER_LT3651",
    "CONFIG_CHARGER_LTC4162L",
    "CONFIG_CHARGER_MANAGER",
    "CONFIG_CHARGER_MAX77976",
    "CONFIG_CHARGER_MAX8903",
    "CONFIG_CHARGER_RT9455",
    "CONFIG_CHARGER_SBS",
    "CONFIG_CHARGER_SMB347",
    "CONFIG_CHARGER_UCS1002",
    "CONFIG_IP5XXX_POWER",
    "CONFIG_PDA_POWER",
    "CONFIG_POWER_SUPPLY_DEBUG",
    "CONFIG_REGULATOR_ACT8865",
    "CONFIG_TEST_POWER",
    "CONFIG_U_SERIAL_CONSOLE",
})


def symbols(text):
    result = {}
    for line in text.splitlines():
        enabled = re.fullmatch(r"(CONFIG_[A-Z0-9_]+)=(.*)", line)
        disabled = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line)
        if not enabled and not disabled:
            continue
        key, value = (enabled.group(1), enabled.group(2)) if enabled else (disabled.group(1), "n")
        if key in result:
            raise ValueError("duplicate configuration symbol: " + key)
        result[key] = value
    return result


def derive(base, mode):
    if mode not in MODES:
        raise ValueError("unknown kernel profile")
    values = symbols(base)
    for name in ("CONFIG_HW_RANDOM", "CONFIG_HW_RANDOM_INGENIC_TRNG"):
        if values.get(name) != "m":
            raise ValueError("accepted base drift: " + name)
    command_line = json.loads(values.get("CONFIG_CMDLINE", "null"))
    if not isinstance(command_line, str) or "rng_core.default_quality" in command_line:
        raise ValueError("accepted base command line drift")
    if mode != "application-minimal":
        return base
    changes = {
        "CONFIG_POWER_SUPPLY": "y",
        "CONFIG_POWER_SUPPLY_HWMON": "n",
        "CONFIG_USB_CONFIGFS_ACM": "y",
        "CONFIG_USB_U_SERIAL": "y",
        "CONFIG_USB_F_ACM": "y",
        "CONFIG_HW_RANDOM": "y",
        "CONFIG_HW_RANDOM_INGENIC_TRNG": "y",
        "CONFIG_CMDLINE": json.dumps(command_line + " " + QUALITY),
        "CONFIG_FB_X1830_USER_PLANE": "y",
    }
    for key, value in changes.items():
        if key in values:
            old = ("# " + key + " is not set" if values[key] == "n"
                   else key + "=" + values[key])
            base, count = re.subn(r"(?m)^" + re.escape(old) + r"$", lambda _: key + "=" + value, base)
            if count != 1:
                raise ValueError("configuration replacement mismatch: " + key)
        else:
            base += "\n" + key + "=" + value + "\n"
    return base


def verify(base, actual, mode):
    expected = symbols(derive(base, mode))
    found = symbols(actual)
    changed = []
    for key in expected.keys() | found.keys():
        if expected.get(key) == found.get(key):
            continue
        dependency_default = mode == "application-minimal" and \
            key in APPLICATION_DEPENDENCY_DEFAULTS and \
            key not in expected and found.get(key) == "n"
        if not dependency_default:
            changed.append(key)
    changed.sort()
    if changed:
        raise ValueError("unexpected profile configuration: " + ",".join(changed))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "verify"))
    parser.add_argument("--mode", choices=MODES, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    if args.base.resolve() == args.config.resolve():
        parser.error("base must be a separate immutable configuration snapshot")
    base = args.base.read_text()
    if args.action == "apply":
        args.config.write_text(derive(base, args.mode))
    else:
        verify(base, args.config.read_text(), args.mode)
    print("GKD_KERNEL_PROFILE_CONFIG=PASS mode=" + args.mode + " action=" + args.action)


if __name__ == "__main__":
    main()
