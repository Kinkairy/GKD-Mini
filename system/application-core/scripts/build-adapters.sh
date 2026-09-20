#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -eu
lane=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd -P)
out=${1:?empty NUC adapter build directory required}
mode=${2:-host}
case "$out" in /tmp/gkd-mini-public/gkd-app-adapters-*) ;; *) exit 2 ;; esac
case "$mode" in host|mips) ;; *) exit 2 ;; esac
image=local/c-builder:2026.08.02-kernel
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
[ ! -e "$out" ]; mkdir -m 700 "$out"
docker run --rm --network none -v "$lane:/lane:ro" -v "$lane/../ui-core:/ui-core:ro" -v "$lane/../config-core:/config-core:ro" -v "$lane/../rc33-system-update/source:/update:ro" -v "$out:/out:rw" \
 -v /srv/c-builder/toolchains/gkdmini-gcw0-d28:/opt/toolchain:ro "$image" sh -eu -c '
mode=$1
cc=cc;link=
if [ "$mode" = mips ]; then cc=/opt/toolchain/host/bin/mipsel-gcw0-linux-uclibc-gcc.br_real;link=-static;fi
flags="-std=gnu99 -Os -Wall -Wextra -Werror -I/lane/include -I/ui-core/include -I/update"
$cc $flags $link /lane/source/gkd-app-card-guard.c -o /out/gkd-app-card-guard
for module in battery media session settings job events lifecycle profile identity namespace idle; do
 $cc $flags -c /lane/source/gkd-app-$module.c -o /out/gkd-app-$module.o
done
if [ "$mode" = host ]; then
 python3 /lane/scripts/derive-schema.py /config-core/schema/gdkmini.schema /out/gdkmini.schema
 $cc $flags /lane/tests/session_child_fixture.c -o /tmp/gkd-session-child
 for variant in normal sanitized; do
  extra=
  [ "$variant" != sanitized ] || extra="-O1 -g -fsanitize=address,undefined"
  $cc $flags $extra /lane/tests/card_guard_fixture.c -Wl,--wrap=stat,--wrap=ioctl -o /out/card-$variant
  /out/card-$variant
  $cc $flags $extra "-DGKD_APP_BATTERY_SYSFS_ROOT=\"/tmp/gkd-battery-sysfs\"" "-DGKD_APP_BATTERY_LED_ROOT=\"/tmp/gkd-battery-leds\"" /lane/source/gkd-app-battery.c /lane/tests/battery_fixture.c -o /out/battery-$variant
  /out/battery-$variant
  $cc $flags $extra "-DGKD_APP_MEDIA_PROC=\"/tmp/gkd-app-media-fixture\"" /lane/source/gkd-app-media.c /lane/tests/media_fixture.c \
   -Wl,--wrap=syscall,--wrap=poll,--wrap=__poll_chk,--wrap=close,--wrap=readdir,--wrap=closedir,--wrap=setns,--wrap=mount,--wrap=umount2,--wrap=waitpid -o /out/media-$variant
  /out/media-$variant
  $cc $flags $extra "-DGKD_APP_HOST_EXECUTABLE=\"/tmp/gkd-session-child\"" /lane/source/gkd-app-session.c /lane/tests/session_fixture.c -o /out/session-$variant
  /out/session-$variant
  awk -F "|" "NF == 8 && \$1 !~ /^#/ {print \$1 \"=\" \$3}" /out/gdkmini.schema >/tmp/gkd-settings.conf
  chmod 0600 /tmp/gkd-settings.conf
  $cc $flags $extra /lane/source/gkd-app-settings.c /lane/tests/settings_fixture.c -o /out/settings-$variant
  /out/settings-$variant /tmp/gkd-settings.conf
  $cc $flags $extra /lane/source/gkd-app-job.c /lane/tests/job_fixture.c -o /out/job-$variant
  /out/job-$variant
  $cc $flags $extra /lane/source/gkd-app-events.c /ui-core/source/gkd-input-owner.c /ui-core/source/gkd-menu-guard.c /lane/tests/events_fixture.c -Wl,--wrap=ioctl -o /out/events-$variant
  /out/events-$variant
  $cc $flags $extra /lane/source/gkd-app-lifecycle.c /lane/tests/lifecycle_fixture.c -o /out/lifecycle-$variant
  /out/lifecycle-$variant
  $cc $flags $extra "-DGKD_APP_PROFILE_RUN=\"/tmp/gkd-profile-cleanup\"" /lane/source/gkd-app-profile.c /lane/tests/profile_cleanup_fixture.c -o /out/profile-$variant
  /out/profile-$variant
  $cc $flags $extra "-DGKD_APP_IDENTITY_DEVICE=\"/tmp/gkd-identity-device\"" \
   "-DGKD_APP_IDENTITY_CID=\"/tmp/gkd-identity-cid\"" "-DGKD_APP_IDENTITY_P1_START=\"/tmp/gkd-identity-start\"" \
   /lane/source/gkd-app-identity.c /update/gkd-update-sha256.c /lane/tests/identity_fixture.c \
   -Wl,--wrap=fstat,--wrap=ioctl -o /out/identity-$variant
  /out/identity-$variant
  $cc $flags $extra /lane/source/gkd-app-idle.c /lane/tests/idle_fixture.c -o /out/idle-$variant
  /out/idle-$variant
  $cc $flags $extra /lane/tests/config_store_fixture.c /lane/source/gkd-app-job.c -Wl,--wrap=stat,--wrap=fsync -o /out/config-store-$variant
  /out/config-store-$variant
  $cc $flags $extra /lane/tests/settings_save_fixture.c /lane/source/gkd-app-job.c \
   -Wl,--wrap=stat -o /out/settings-save-$variant
  /out/settings-save-$variant
 done
fi
cd /out
sha256sum gkd-app-card-guard gkd-app-battery.o gkd-app-media.o gkd-app-session.o gkd-app-settings.o gkd-app-job.o gkd-app-events.o gkd-app-lifecycle.o gkd-app-profile.o gkd-app-identity.o gkd-app-namespace.o gkd-app-idle.o >SHA256SUMS
' sh "$mode"
printf 'GKD_APP_ADAPTERS_BUILD=PASS mode=%s\n' "$mode"
