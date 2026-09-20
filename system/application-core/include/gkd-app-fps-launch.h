/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_FPS_LAUNCH_H
#define GKD_APP_FPS_LAUNCH_H
#ifndef GKD_APP_FPS_INTERPOSER
#define GKD_APP_FPS_INTERPOSER "/var/run/gkd-app/libgkd-fps-present.so"
#endif
struct gkd_app_fps_launch {
    int executable_fd, counter_fd, lifetime_fd;
    char session[33];
    char *preload;
};
#define GKD_APP_FPS_LAUNCH_INIT {.executable_fd=-1,.counter_fd=-1,.lifetime_fd=-1}
void gkd_app_fps_launch_init(struct gkd_app_fps_launch *);
void gkd_app_fps_launch_close(struct gkd_app_fps_launch *);
/* Best-effort FPS setup. Valid API calls return zero even when collection is
 * unavailable; game launch remains unchanged in that case. */
int gkd_app_fps_launch_prepare(struct gkd_app_fps_launch *,const char *executable,
                               const char *directory,const char *existing_preload);
#endif
