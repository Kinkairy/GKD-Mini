/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_MEDIA_H
#define GKD_APP_MEDIA_H
#include <sys/types.h>
#define GKD_APP_MEDIA_MAX_TASKS 64U
#define GKD_APP_MEDIA_MAX_VIEWS 64U
struct gkd_app_media_view {
    int rootfd, mntnsfd, mountsfd;
    dev_t ns_dev;
    ino_t ns_ino;
    int mounted;
};
struct gkd_app_media {
    int init_pidfd, pidnsfd;
    pid_t init_pid, host_pid;
    pid_t pids[GKD_APP_MEDIA_MAX_TASKS];
    int pidfds[GKD_APP_MEDIA_MAX_TASKS];
    unsigned char was_stopped[GKD_APP_MEDIA_MAX_TASKS];
    unsigned char stopped_by_us[GKD_APP_MEDIA_MAX_TASKS];
    struct gkd_app_media_view views[GKD_APP_MEDIA_MAX_VIEWS];
    unsigned count, view_count;
    int paused, mounted;
};
#define GKD_APP_MEDIA_INIT { .init_pidfd = -1, .pidnsfd = -1, .mounted = 1 }
void gkd_app_media_init(struct gkd_app_media *);
int gkd_app_media_pause(struct gkd_app_media *, pid_t init_pid, pid_t expected_host_pid, unsigned timeout_ms);
/* Reconcile an interrupted mount helper before any explicit recovery. */
int gkd_app_media_probe(struct gkd_app_media *);
int gkd_app_media_game_unmount(struct gkd_app_media *);
int gkd_app_media_game_mount(struct gkd_app_media *);
int gkd_app_media_resume(struct gkd_app_media *);
int gkd_app_media_release_dead(struct gkd_app_media *media);
void gkd_app_media_close(struct gkd_app_media *);
#endif
