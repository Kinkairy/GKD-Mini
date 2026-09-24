#!/usr/bin/env python3
"""A profile uses the single voltage curve, with no alternate battery backend."""
from pathlib import Path
import sys
source, output = map(Path, sys.argv[1:])
removed = {"battery_voltage_fallback", "battery_voltage_thresholds_mv"}
lines = source.read_text().splitlines(keepends=True)
observed = [line.split("|", 1)[0] for line in lines if line.split("|", 1)[0] in removed]
if set(observed) != removed or len(observed) != len(removed) or output.exists():
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED source-contract")
# A previously rendered English regardless of the shared default. Preserve
# that presentation until the owner explicitly selects cn (stored as zh).
language = "ui_language|enum|zh|||zh,en|ui-stack|next-entry" + chr(10)
if lines.count(language) != 1:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED language-contract")
lines = [line.replace("ui_language|enum|zh|", "ui_language|enum|en|", 1)
         if line == language else line for line in lines]
# A reserves MENU as a system prefix; shoulders alone retain zero-wait gameplay.
hotkey = "screenshot_hotkey|hotkey|L1+L2|||shoulder_pair_or_none|screenshot|keys-released" + chr(10)
if lines.count(hotkey) != 1:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED hotkey-contract")
lines = [line.replace("|L1+L2|", "|MENU+L1|", 1) if line == hotkey else line for line in lines]
# Owner default is ten minutes; preserve the shared 600-second default.
sleep = "auto_suspend_timeout_seconds|uint|600|0|86400||hardware|immediate" + chr(10)
if lines.count(sleep) != 1:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED sleep-contract")
# Owner starts the single POWER LOW warning at the same 5% threshold as
# the suspend grace. Shared/R schema defaults remain unchanged.
low_rows = [i for i, line in enumerate(lines) if line.startswith("battery_low_percent|")]
if len(low_rows) != 1:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED battery-low-contract")
parts = lines[low_rows[0]].rstrip("\n").split("|")
if parts[2] != "10":
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED battery-low-default")
parts[2] = "5"
lines[low_rows[0]] = "|".join(parts) + "\n"
# Critical-battery policy is A-only; the frozen recovery profile is unchanged.
lines += [
    "battery_suspend_enabled|uint|1|0|1||battery|immediate\n",
    "battery_suspend_delay_ms|uint|15000|1000|600000||battery|immediate\n",
]
# A labels share one catalog with the renderer; no second list of defaults.
import json, re
catalog = Path(__file__).resolve().parents[2] / "ui-core/include/gkd-ui-language.def"
glyphs = "".join(sorted(set(c for c in catalog.read_text() if ord(c)>127)))
for row in catalog.read_text().splitlines():
    if not row.startswith("GKD_UI_TEXT_ROW("): continue
    match = re.fullmatch(r'GKD_UI_TEXT_ROW\([A-Z_]+, ([a-z_]+), ("[^"\n]*"), ("[^"\n]*")\)', row)
    if not match: raise SystemExit("GKD_APP_SCHEMA=BLOCKED catalog-contract")
    key, en, zh = match.groups()
    for language, value in (("en", en), ("zh", zh)):
        # Card page bodies follow the shared English-only text contract.
        body = key in ("insert_card", "loading_card")
        limit = (3 if language == "zh" else 7) if key in ("yes", "no") else (9 if language == "zh" and not body else 20)
        alphabet = glyphs if language == "zh" and not body else "ASCII"
        lines.append(f"app_text_{key}_{language}|ui_text|{json.loads(value)}|1|{limit}|{alphabet}|ui-text|next-entry\n")
# Compile the same A contract that the runtime C reader includes. Do not
# expand R's hotkey/path policy or silently accept values A cannot load.
contract = (Path(__file__).resolve().parents[1] / "include/gkd-app-settings-contract.def").read_text()
paths = re.findall(r'^GKD_SCREENSHOT_PATH\("([^"\n]+)", ([0-9]+)\)$', contract, re.M)
hotkeys = re.findall(r'^GKD_SCREENSHOT_HOTKEY\("([^"\n]+)", ([0-9]+), ([0-9]+)\)$', contract, re.M)
if len(paths) != 1 or not hotkeys or len({x[0] for x in hotkeys}) != len(hotkeys):
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED settings-contract")
path_root, path_bytes = paths[0]
if not path_root.startswith("/") or not path_root.endswith("/") or int(path_bytes) < len(path_root)+2:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED path-contract")
runtime = (Path(__file__).resolve().parents[1] / "source/gkd-app-settings.c").read_text()
unique_keys = re.findall(r'T\("([a-z_]+)",KEY_FIELD,', runtime)
if not unique_keys or len(set(unique_keys)) != len(unique_keys):
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED key-contract")
changed = set()
for index, line in enumerate(lines):
    parts = line.rstrip("\n").split("|")
    if len(parts) != 8:
        continue
    key = parts[0]
    if key == "screenshot_hotkey":
        parts[1], parts[5] = "enum", ",".join(row[0] for row in hotkeys)
        if parts[2] not in {row[0] for row in hotkeys}:
            raise SystemExit("GKD_APP_SCHEMA=BLOCKED hotkey-default")
    elif key == "screenshot_output_dir":
        parts[1], parts[4], parts[5] = "safe_subdir", str(int(path_bytes)-1), path_root
    elif key in unique_keys:
        parts[1] = "unique_keycode"
    else:
        continue
    changed.add(key)
    lines[index] = "|".join(parts)+"\n"
if changed != set(unique_keys) | {"screenshot_hotkey", "screenshot_output_dir"}:
    raise SystemExit("GKD_APP_SCHEMA=BLOCKED settings-schema-coverage")
output.write_text("".join(line for line in lines if line.split("|", 1)[0] not in removed))
print("GKD_APP_SCHEMA=PASS")
