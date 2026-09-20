/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_TRANSFER_H
#define GKD_APP_TRANSFER_H
#include "gkd-app-ready.h"
enum gkd_app_transfer_state {
    GKD_TRANSFER_WAITING = 0, GKD_TRANSFER_RECEIVED, GKD_TRANSFER_CANCELLED,
    GKD_TRANSFER_TIMED_OUT, GKD_TRANSFER_BAD_EVENT, GKD_TRANSFER_CHANNEL_ERROR,
    GKD_TRANSFER_TAKEN
};
struct gkd_app_transfer {
    int fd, init_fd, child_fd;
    pid_t owner, init_pid, child_pid;
    uid_t init_uid;
    unsigned char token[GKD_APP_READY_TOKEN_BYTES];
    uint64_t last_ms, deadline_ms;
    enum gkd_app_transfer_state state;
};
#define GKD_APP_TRANSFER_INIT { .fd = -1, .init_fd = -1, .child_fd = -1, \
    .state = GKD_TRANSFER_CHANNEL_ERROR }
struct gkd_app_transfer_tx { int sent; };
#define GKD_APP_TRANSFER_TX_INIT { .sent = 0 }

/* Before watch_init, on the existing watch_error_channel [0] receiver.
 * Borrows fd and pinned init_fd (distinct CLOEXEC >=3); owns only a successfully
 * received child_fd until take/close. Initialize with macro; close before reuse.
 * Trusted matching /proc, single controller owner and blocked app required.
 * Preparation/root integrity and exclusive FD provenance remain caller duties.
 */
int gkd_app_transfer_init(struct gkd_app_transfer *rx, int fd, int init_fd,
    pid_t init_pid, uid_t init_uid, const unsigned char token[16],
    uint64_t now_ms, uint64_t timeout_ms);
enum gkd_app_transfer_state gkd_app_transfer_poll(struct gkd_app_transfer *rx, uint64_t now_ms);
/* Received observation is not health. Take rechecks identity/liveness and hands
 * ownership to caller, once. Caller then arms watch before releasing the app.
 */
int gkd_app_transfer_take(struct gkd_app_transfer *rx, pid_t *child_pid);
void gkd_app_transfer_close(struct gkd_app_transfer *rx);
const char *gkd_app_transfer_name(enum gkd_app_transfer_state state);

/* Preparation's one nonblocking send on watch_error_channel [1]. Borrows the
 * pinned direct-child pidfd; never closes/reaps. No retry policy or timeout here:
 * caller bounds EAGAIN retries, closes copies, then execs sealed existing init.
 */
int gkd_app_transfer_send(struct gkd_app_transfer_tx *tx, int fd, int child_fd,
    pid_t child_pid, const unsigned char token[16]);
#endif
