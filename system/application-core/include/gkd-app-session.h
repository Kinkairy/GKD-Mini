/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_SESSION_H
#define GKD_APP_SESSION_H
#include "gkd-app-service.h"
#include <sys/types.h>
#include <stdint.h>
enum gkd_app_session_state {
    GKD_SESSION_IDLE, GKD_SESSION_STARTING, GKD_SESSION_READY,
    GKD_SESSION_STOPPING, GKD_SESSION_STOPPED, GKD_SESSION_FAILED
};
struct gkd_app_session {
    pid_t pid;
    int pidfd, channel, status, forced, error;
    uint64_t deadline, last_clock;
    enum gkd_app_session_state state;
    struct gkd_app_service_ready ready;
};
#define GKD_APP_SESSION_INIT { .pid=-1, .pidfd=-1, .channel=-1, .status=-1 }
int gkd_app_session_start(struct gkd_app_session *, unsigned timeout_ms, uint64_t now);
enum gkd_app_session_state gkd_app_session_poll(struct gkd_app_session *, uint64_t now);
int gkd_app_session_stop(struct gkd_app_session *, uint64_t now);
/* Refuses to discard a live or unreaped child. */
int gkd_app_session_close(struct gkd_app_session *);
#endif
