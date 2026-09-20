#!/usr/bin/env python3
"""Extract the A PMIC property functions and register constants from shipped patches."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
project=Path(__file__).resolve().parents[3]
patches=project/"kernel/current/patches"
relative="drivers/input/keyboard/gkd-axp173-pek.c"
base=patches/"0001-rc34-accepted-kernel.patch"
delta=patches/"0003-application-power-supply.patch"
assert hashlib.sha256(base.read_bytes()).hexdigest()=="9740d17d106046163fd3e1aca0a32b1b02a85b29a44ea14392670bcf199d8045"
with tempfile.TemporaryDirectory(prefix="gkd-power-driver-test-") as temp:
    for patch in (base,delta):
        subprocess.run(["git","apply","--include="+relative,str(patch)],cwd=temp,check=True)
    driver=(Path(temp)/relative).read_text()
assert "AXP173_CAPACITY" not in driver and "POWER_SUPPLY_PROP_CAPACITY" not in driver
names=("gkd_axp173_read_byte","gkd_axp173_battery_get_property","gkd_axp173_usb_get_property")
parts=[]
for name in names:
    matches=list(re.finditer(r"^static [^;{]*\b"+name+r"\([^;{]*\)\s*\{",driver,re.M))
    assert len(matches)==1,name
    match=matches[0];end=match.end();depth=1
    while depth:
        depth+=(driver[end]=="{")-(driver[end]=="}");end+=1
    parts.append(driver[match.start():end])
constants=re.findall(r"^#define AXP173_(?:CHARGE_STATUS|STATUS_USB_VALID|BATTERY_VOLTAGE_H|BATTERY_VOLTAGE_L)\b[^\n]*",driver,re.M)
assert len(constants)==4
fixture=Path(__file__).with_name("application_power_fixture.c").read_text()
assert fixture.count("/* DRIVER_FRAGMENT */")==1
Path(sys.argv[1]).write_text(fixture.replace("/* DRIVER_FRAGMENT */","\n".join(constants+parts)))
print("GKD_APP_POWER_EXTRACTION="+hashlib.sha256(delta.read_bytes()).hexdigest())
