#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
project=$(CDPATH= cd -- "$lane/../.." && pwd -P)
out=${1:?empty output directory required}
mode=${2:-mips}
case "$out" in /tmp/gkd-mini-public/gkd-app-service-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
[ ! -e "$out" ]; mkdir -m 0700 "$out"
docker run --rm --network none --user "$(id -u):$(id -g)" \
 -v "$project:/src:ro" -v "$out:/out:rw" \
 -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro "$image" sh -eu -c '
mode=$1
cc=cc; link="-Wl,--gc-sections";strip=strip
if [ "$mode" = mips ]; then
 cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real
 strip=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-strip
 link="$link -static"
fi
app=/src/system/application-core;ui=/src/system/ui-core;update=/src/system/rc33-system-update/source
sources="$app/source/gkd-application-service.c"
for module in session media lifecycle settings job identity battery events profile namespace idle fps; do
 sources="$sources $app/source/gkd-app-$module.c"
done
for module in ui input-owner ui-plane-client menu-guard ui-language r-poweroff; do
 sources="$sources $ui/source/gkd-$module.c"
done
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -DGKD_APPLICATION_UI=1 \
 -ffunction-sections -fdata-sections -I"$app/include" -I"$ui/include" -I"$update" \
 $sources "$update/gkd-update-sha256.c" $link -o /out/gkd-application-service
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -DGKD_APPLICATION_UI=1 \
 -ffunction-sections -fdata-sections "$ui/source/gkd-screenshot.c" $link -o /out/gkd-screenshot
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -I"$app/include" -I"$ui/include" \
 "$app/source/gkd-app-config-store.c" "$app/source/gkd-app-job.c" $link -o /out/gkd-app-config-store
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror \
 -I"$app/include" "$app/source/gkd-app-game.c" "$app/source/gkd-app-namespace.c" \
 "$app/source/gkd-app-game-control.c" $link -o /out/gkd-app-game
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -DGKD_APPLICATION_UI=1 -I"$ui/include" \
 "$ui/source/gkd-input.c" $link -o /out/gkd-input
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror \
 "$app/source/gkd-app-management.c" $link -o /out/gkd-app-management
"$cc" -std=gnu99 -Os -Wall -Wextra -Werror -I"$app/include" -I"$ui/include" "$app/source/gkd-app-menu-config.c" "$app/source/gkd-menu-config.c" $link -o /out/gkd-menu-config
"$strip" /out/gkd-app-management /out/gkd-menu-config /out/gkd-application-service /out/gkd-screenshot /out/gkd-app-config-store /out/gkd-app-game /out/gkd-input
cd /out;sha256sum gkd-menu-config gkd-app-management gkd-application-service gkd-screenshot gkd-app-config-store gkd-app-game gkd-input >SHA256SUMS
' sh "$mode"
if [ "$mode" = mips ]; then
 file "$out/gkd-application-service" | grep -q 'ELF 32-bit LSB executable, MIPS'
 ! readelf -l "$out/gkd-application-service" | grep -q INTERP
 ! readelf -l "$out/gkd-screenshot" | grep -q INTERP
 ! strings "$out/gkd-screenshot" | grep -F -e /dev/mem -e gkd_screenshot_osd
fi
printf 'GKD_APP_SERVICE_BUILD=PASS mode=%s\n' "$mode"
