#!/usr/bin/env python3
"""Render the reviewed minimum application manifest; no discovery or fallback."""
import json
from pathlib import Path
import re
import sys

def render(data):
    def digest(value):
        if not re.fullmatch("[0-9a-f]{64}", value):
            raise ValueError("invalid digest")
        return value
    animation = data["boot_animation"]
    if animation["path"] != "/boot/gkd-mini/startup-animation.rgb565":
        raise ValueError("unsafe animation path")
    lines = [
        '#define APP_ANIMATION_PATH "' + animation["path"] + '"',
        '#define APP_ANIMATION_SHA256 "' + digest(animation["sha256"]) + '"',
        '#define APP_OPK_SHA256 "' + digest(data["opk_sha256"]) + '"',
        '#define APP_SM_SHA256 "' + digest(data["sm_sha256"]) + '"',
        "static const struct app_file app_files[] = {"]
    seen = set()
    for item in data["dependencies"]:
        for key in ("path", "resolved"):
            path = item[key]
            if not path.startswith(("/lib/", "/usr/lib/")) or ".." in path or not re.fullmatch(r"[/A-Za-z0-9_.+-]+", path):
                raise ValueError("unsafe dependency path")
        if item["path"] in seen:
            raise ValueError("duplicate dependency")
        seen.add(item["path"])
        lines.append('    {"%s", "%s", "%s"},' % (item["path"], item["resolved"], digest(item["sha256"])))
    if not seen:
        raise ValueError("empty dependency manifest")
    return "\n".join(lines + ["};", ""])

if __name__ == "__main__":
    with Path(sys.argv[2]).open("x", encoding="ascii") as out:
        out.write(render(json.loads(Path(sys.argv[1]).read_text())))
