#!/bin/sh
set -eu
output=${1:?new OPK output required}
[ ! -e "$output" ]
tmp=$(mktemp -d /tmp/gkd-mini-public/gkdsu-opk.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
printf '%s\n' '[Desktop Entry]' 'Name=系统更新' \
  'Comment=从游戏卡安装签名系统更新' 'Exec=/usr/sbin/gkd-system-update' \
  'Terminal=false' 'Type=Application' 'StartupNotify=false' \
  'Categories=applications;' >"$tmp/default.gcw0.desktop"
mksquashfs "$tmp" "$output" -noappend -all-root -no-xattrs -comp gzip >/dev/null
unsquashfs -d "$tmp/readback" -no-progress "$output" default.gcw0.desktop >/dev/null
grep -qx 'Exec=/usr/sbin/gkd-system-update' "$tmp/readback/default.gcw0.desktop"
echo 'GKDSU_OPK=PASS'
