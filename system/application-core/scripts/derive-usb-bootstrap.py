#!/usr/bin/env python3
"""Reuse the accepted management bootstrap, with no runtime export/rebuild API."""
from pathlib import Path
import argparse
import re

def derive(text):
    for name in ("add_system_lun", "mounted_locally", "export_start", "export_stop", "stop_all", "status"):
        pattern = r"(?m)^" + name + r"\(\)\n\{\n.*?^\}\n\n"
        text, count = re.subn(pattern, "", text, flags=re.DOTALL)
        if count != 1:
            raise ValueError("bootstrap source contract: " + name)
    old = '\t[ "$mode" = network ] || add_system_lun || return 1\n'
    if text.count(old) != 1:
        raise ValueError("bootstrap mode contract")
    text = text.replace(old, "")
    # The A cold bootstrap never creates a storage function; its failure cleanup
    # contains only resources this one operation can have created.
    old_lun = """\t\t[ ! -e "$gadget/functions/mass_storage.0/lun.0/file" ] ||
\t\t\twrite_attr "$gadget/functions/mass_storage.0/lun.0/file" '' \\
\t\t\t\t>/dev/null 2>&1 || failed=1
"""
    if text.count(old_lun) != 1:
        raise ValueError("bootstrap cleanup contract")
    text = text.replace(old_lun, "")
    text = text.replace(' \\\n\t\t\t\t"$gadget/configs/c.1/mass_storage.0"', "")
    text = text.replace('"$gadget/functions/mass_storage.0" \\\n\t\t\t\t', "")
    if "mass_storage" in text:
        raise ValueError("bootstrap storage residual")
    marker = '[ "$#" = 1 ] || {'
    if text.count(marker) != 1:
        raise ValueError("bootstrap dispatch contract")
    text = text[:text.index(marker)] + r"""[ "$#" = 1 ] && [ "$1" = network-start ] || {
    echo 'usage: gkd-application-bootstrap-usb network-start' >&2
    exit 64
}
# Sole cold-boot entry. A's service owns every subsequent mode transition.
[ ! -e "$gadget" ] || blocked already-bootstrapped
lock_root=/run/gkd-recovery
[ "$testing" != 1 ] || lock_root=${GKD_RECOVERY_USB_TEST_ROOT:?}/operations
mkdir -p "$lock_root" || blocked operation-lock
mkdir "$lock_root/card-operation.lock" 2>/dev/null || blocked operation-busy
trap 'rmdir "$lock_root/card-operation.lock"' EXIT
trap 'exit 1' HUP INT TERM
network_start
"""
    return text.replace(
        "# Dedicated-R USB owner: RNDIS/SSH by default, with an explicit whole-system\n"
        "# card LUN only while EXPORT SYSTEM CARD is active.",
        "# A cold-boot management setup, derived from the accepted shared R bootstrap."
    )

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.output.open("x") as stream:
        stream.write(derive(args.source.read_text()))
    args.output.chmod(0o555)

if __name__ == "__main__":
    main()
