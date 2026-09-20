#!/usr/bin/env python3
"""Actual service orchestration with real command children and isolated leaf fixtures."""
import pathlib
import subprocess
import sys
project = pathlib.Path(__file__).resolve().parents[3]
out = pathlib.Path(sys.argv[1])
if not str(out).startswith("/tmp/gkd-mini-public/gkd-app-service-test-") or out.exists():
    raise SystemExit("new NUC test directory required")
out.mkdir(mode=0o700)
schema = out / "gdkmini.schema"
subprocess.run(["python3",str(project/"system/application-core/scripts/derive-schema.py"),
                str(project/"system/config-core/schema/gdkmini.schema"),str(schema)],check=True)
defaults = "\n".join(parts[0]+"="+parts[2] for line in schema.read_text().splitlines()
                    if not line.startswith("#") and len(parts := line.split("|")) == 8)+"\n"
(out/"effective.conf").write_text(defaults)
subprocess.run(["python3",str(project/"system/ui-core/scripts/generate-psf2.py"),
                "--source","/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1/lib/fonts/font_8x16.c",
                "--output",str(out/"fallback.psf")],check=True)
image = "local/c-builder:2026.08.02-kernel"
assert subprocess.check_output(["docker","image","inspect",image,"--format","{{.Id}}"], text=True).strip() == "sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1"
base = ["docker","run","--rm","--network","none","-v",str(project)+":/src:ro","-v",str(out)+":/out:rw",image]
app="/src/system/application-core";ui="/src/system/ui-core";update="/src/system/rc33-system-update/source"
sources=[app+"/tests/service_fixture.c"]
sources += [app+"/source/gkd-app-"+module+".c" for module in
            ("session","media","lifecycle","settings","job","identity","battery","events","profile","namespace","idle","fps")]
sources += [ui+"/source/gkd-"+module+".c" for module in
            ("ui","ui-language","input-owner","ui-plane-client","menu-guard","r-poweroff")]
sources += [update+"/gkd-update-sha256.c"]
wrappers=("gkd_app_fps_authorize_launcher","gkd_app_idle_lease_acquire","gkd_app_idle_lease_live","gkd_app_idle_lease_release","gkd_app_identity_read","gkd_app_battery_read","gkd_app_battery_led","gkd_app_media_pause","gkd_app_media_game_unmount","gkd_app_media_game_mount",
          "gkd_app_media_resume","gkd_app_session_stop","gkd_app_session_start","gkd_app_session_poll",
          "gkd_app_identity_verify","gkd_app_profile_cleanup","ioctl","stat","gkd_app_events_open","gkd_app_events_close","gkd_app_events_open_menu","gkd_app_events_open_menu_reuse","gkd_ui_render_status","gkd_ui_render_loading","gkd_ui_render_confirmation_info","gkd_ui_menu_send","gkd_ui_menu_hide","gkd_ui_menu_clear","gkd_ui_export_osd_argb","gkd_ui_plane_send","gkd_ui_plane_clear")
for variant,extra in (("normal",[]),("sanitized",["-O1","-g","-fsanitize=address,undefined"])):
    subprocess.run(base+["cc","-std=gnu99","-Os","-Wall","-Wextra","-Werror","-DGKD_APPLICATION_UI=1",
                        "-I"+app+"/include","-I"+ui+"/include","-I"+update]+extra+sources+
                   ["-Wl,"+",".join("--wrap="+name for name in wrappers),"-o","/out/service-"+variant],check=True)
    subprocess.run(base+["sh","-ec","cp /out/effective.conf /tmp/gkd-service-effective.conf\nchmod 0600 /tmp/gkd-service-effective.conf\n/out/service-"+variant],check=True)
    subprocess.run(base+["cc","-std=gnu99","-Os","-Wall","-Wextra","-Werror"]+extra+
                   ["-I"+app+"/include","-I"+ui+"/include",app+"/tests/settings_fixture.c",app+"/source/gkd-app-settings.c",
                    "-o","/out/settings-"+variant],check=True)
    subprocess.run(base+["sh","-ec","cp /out/effective.conf /tmp/gkd-service-effective.conf\nchmod 0600 /tmp/gkd-service-effective.conf\n/out/settings-"+variant+" /tmp/gkd-service-effective.conf"],check=True)
    subprocess.run(base+["cc","-std=gnu99","-Os","-Wall","-Wextra","-Werror"]+extra+
                   ["-I"+ui+"/include",ui+"/source/gkd-ui-language.c",ui+"/tests/catalog_fixture.c",
                    "-o","/out/catalog-"+variant],check=True)
    subprocess.run(base+["/out/catalog-"+variant],check=True)
print("GKD_APP_SERVICE_TESTS=PASS normal/ASan/UBSan")
