/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_CONTROLS_H
#define GKD_APP_CONTROLS_H

#include <stdint.h>
#include <sys/types.h>

enum gkd_app_controls_state {
	GKD_APP_CONTROLS_IDLE = 0,
	GKD_APP_CONTROLS_WAITING,
	GKD_APP_CONTROLS_READY,
	GKD_APP_CONTROLS_STOPPING,
	GKD_APP_CONTROLS_REAPED,
	GKD_APP_CONTROLS_FAILED
};

struct gkd_app_controls {
	pid_t pid;
	int pidfd;
	int statefd;
	int readyfd;
	uint64_t deadline_ms;
	uint64_t stop_deadline_ms;
	uint64_t kill_deadline_ms;
	uint64_t last_now_ms;
	int exit_status;
	enum gkd_app_controls_state state;
};

#define GKD_APP_CONTROLS_INIT { .pid = -1, .pidfd = -1, .statefd = -1, .readyfd = -1, .exit_status = -1, .state = GKD_APP_CONTROLS_IDLE }

int gkd_app_controls_start(struct gkd_app_controls *, int init_pidfd, pid_t init_pid,
	uint64_t now_ms);
enum gkd_app_controls_state gkd_app_controls_poll(struct gkd_app_controls *, uint64_t now_ms);
enum gkd_app_controls_state gkd_app_controls_stop(struct gkd_app_controls *, uint64_t now_ms);
void gkd_app_controls_close(struct gkd_app_controls *);

#endif
