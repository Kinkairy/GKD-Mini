#!/usr/bin/env python3
"""Extract the tested freeze paths unchanged from the final driver."""
from pathlib import Path
import hashlib
import re
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: extract_freeze_test.py DRIVER FIXTURE OUTPUT")

driver_path = Path(sys.argv[1])
fixture_path = Path(sys.argv[2])
output_path = Path(sys.argv[3])
driver = driver_path.read_text()

names = [
    "x1830_debug_frozen_release_locked",
    "x1830_debug_frozen_capture_locked",
    "x1830_user_debug_freeze_release_locked",
    "x1830_user_debug_freeze_live_locked",
    "x1830_overlay_active_locked",
    "x1830_composite_refresh_work",
    "gkd_ui_freeze_submit_ioctl",
    "gkd_ui_freeze_clear_ioctl",
    "x1830_fb_ioctl",
    "x1830_fb_pan_display",
]

fragments = []
by_name = {}
for name in names:
    matches = list(re.finditer(
        r"^static [^;{]*\b" + re.escape(name) + r"\([^;{]*\)\s*\{",
        driver,
        re.M,
    ))
    assert len(matches) == 1, (name, len(matches))
    match = matches[0]
    end = match.end()
    depth = 1
    while depth:
        depth += (driver[end] == "{") - (driver[end] == "}")
        end += 1
    fragment = driver[match.start():end]
    fragments.append(fragment)
    by_name[name] = fragment

submit_and_clear = by_name["gkd_ui_freeze_submit_ioctl"] + by_name["gkd_ui_freeze_clear_ioctl"]
assert "usb_debug_freeze" not in submit_and_clear
assert "gkd_usb_menu" not in submit_and_clear
assert not re.search(r"\b(?:k|v|dma_)alloc", submit_and_clear)
assert "thread_group_exited" in by_name["x1830_user_debug_freeze_live_locked"]
assert "x1830_debug_frozen_capture_locked" in by_name["gkd_ui_freeze_submit_ioctl"]
assert "x1830_debug_frozen_release_locked" in by_name["x1830_user_debug_freeze_release_locked"]
assert "x1830_user_debug_freeze_release_locked(fb);" in driver[driver.index("static int x1830_fb_remove"):]
assert "#if IS_ENABLED(CONFIG_FB_X1830_USER_PLANE)\n\tcase GKD_UI_FREEZE_SUBMIT" in driver

fixture = fixture_path.read_text()
assert fixture.count("/* DRIVER_FRAGMENT */") == 1
output_path.write_text(fixture.replace("/* DRIVER_FRAGMENT */", "\n\n".join(fragments)))
print("FINAL_DRIVER_SHA256=" + hashlib.sha256(driver_path.read_bytes()).hexdigest())
print("EXTRACTED_FUNCTIONS=" + ",".join(names))

