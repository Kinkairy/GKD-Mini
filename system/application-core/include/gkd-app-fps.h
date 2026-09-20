/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_FPS_H
#define GKD_APP_FPS_H
#include "gkd-fps-counter.h"
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
enum gkd_app_fps_state { GKD_APP_FPS_NO_GAME, GKD_APP_FPS_UNAVAILABLE, GKD_APP_FPS_AVAILABLE };
struct gkd_app_fps_context {
    pid_t host, init;
    int lifecycle_allows_game;
    int enabled;
};
struct gkd_app_fps_view {
    enum gkd_app_fps_state state;
    unsigned fps;
    int visible;
};
struct gkd_app_fps {
    int page_fd, lifetime_fd, peer_pidfd;
    const struct gkd_fps_counter_page *page;
    pid_t peer, host, init;
    uint64_t session_hi, session_lo, sample_ms;
    uint32_t previous;
    unsigned fps;
    enum gkd_app_fps_state state;
};
#define GKD_APP_FPS_INIT {.page_fd=-1,.lifetime_fd=-1,.peer_pidfd=-1,.state=GKD_APP_FPS_NO_GAME}
int gkd_app_fps_authorize_launcher(const struct ucred *,const struct gkd_app_fps_context *,
                                  int *,unsigned long long *);
void gkd_app_fps_init(struct gkd_app_fps *);
void gkd_app_fps_close(struct gkd_app_fps *);
/* Called after accept + SO_PEERCRED and before ordinary recv.
 * 0 leaves a non-FPS packet queued; 1 consumed/replied to an FPS packet. */
int gkd_app_fps_control(struct gkd_app_fps *,int client,const struct ucred *,
                        const struct gkd_app_fps_context *,uint64_t now_ms);
void gkd_app_fps_update(struct gkd_app_fps *,
                        const struct gkd_app_fps_context *,uint64_t now_ms);
struct gkd_app_fps_view gkd_app_fps_read(const struct gkd_app_fps *,
                                         const struct gkd_app_fps_context *);
#endif
