#!/bin/sh
set -eu
lane=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/gkd-mini-public/gkdsu-menu-test.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
root=$tmp/card
game=$tmp/game
mkdir -p "$root/local/home/.simplemenu/section_groups" "$root/system-tools" "$game"
cp "$lane/tests/fixtures/system.ini" \
  "$root/local/home/.simplemenu/system-section.ini.disabled"
printf 'opk-fixture\n' >"$tmp/update.opk"
printf 'runtime-fixture\n' >"$tmp/runtime"
if "$lane/scripts/install-menu-entry.sh" "$tmp/update.opk" "$tmp/runtime" "$root" "$game" >/dev/null 2>&1; then
 echo 'retired installer unexpectedly succeeded' >&2; exit 1
fi
[ ! -e "$root/local/home/.simplemenu/section_groups/system.ini" ]
[ ! -e "$root/system-tools/system-update.opk" ]
[ ! -e "$game/gkd-update/gkd-update-runtime" ]
echo 'GKDSU_MENU_RETIRED_TEST=PASS'
