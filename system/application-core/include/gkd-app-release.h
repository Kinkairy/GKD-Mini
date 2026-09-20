/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_RELEASE_H
#define GKD_APP_RELEASE_H
#include "gkd-app-watch.h"

enum gkd_app_release_state {
    GKD_RELEASE_WAITING = 0, GKD_RELEASE_GRANTED, GKD_RELEASE_CANCELLED,
    GKD_RELEASE_TIMED_OUT, GKD_RELEASE_BAD_EVENT, GKD_RELEASE_CHANNEL_ERROR
};
struct gkd_app_release_rx {
    int fd;
    pid_t owner, sender_pid;
    uid_t sender_uid;
    unsigned char token[GKD_APP_READY_TOKEN_BYTES];
    uint64_t last_ms, deadline_ms;
    enum gkd_app_release_state state;
};
#define GKD_APP_RELEASE_RX_INIT { .fd = -1, .state = GKD_RELEASE_CHANNEL_ERROR }
struct gkd_app_release_tx { int sent; };
#define GKD_APP_RELEASE_TX_INIT { .sent = 0 }

/* Separate temporary release pair: controller sends on [0], child receives on
 * [1]. Only [1] enables SO_PASSCRED; no reply is sent on this channel. Keep the
 * existing exec-error pair separate. Caller closes both release handles after
 * transition; release handles must never survive exec or be reused as errors.
 */
int gkd_app_release_channel(int pair[2]);
/* Borrow connected anonymous NONBLOCK/CLOEXEC FD >=3; never close it here.
 * Init in the receiving process. Exact sender PID/UID are expressed in that
 * process's namespaces. PID 0 is allowed ONLY as an exact ancestor-namespace
 * mapping, never a wildcard/unique identity: private endpoint + fresh token
 * and trusted preparation provenance are mandatory. No privileged sandbox.
 * Caller owns deadline/config policy and must _exit on any non-granted terminal
 * result. A grant permits one transition to exec; it is not health evidence.
 */
int gkd_app_release_rx_init(struct gkd_app_release_rx *rx, int fd, pid_t sender_pid,
                          uid_t sender_uid, const unsigned char token[16],
                          uint64_t now_ms, uint64_t timeout_ms);
enum gkd_app_release_state gkd_app_release_poll(struct gkd_app_release_rx *rx,
                                               uint64_t now_ms);
/* Single controller, no concurrent FD/watch mutations. Borrow init handles and
 * watch. Refresh init image + existing watch before one nonblocking send.
 * Success=0 sets sent; repeated success is refused with EALREADY. Failure=-1:
 * EAGAIN for nonmatching init image/full queue; caller must bound retries by
 * the watch deadline. EPIPE/ESRCH/ETIMEDOUT/EPROTO indicate terminal refusal.
 * Sender copies/provenance must be verified before calling. No sleep or reap.
 */
int gkd_app_release_send(struct gkd_app_release_tx *tx, struct gkd_app_watch *watch, int release_fd,
                         int init_pidfd, pid_t init_pid, int init_executable_fd,
                         uint64_t now_ms);
const char *gkd_app_release_name(enum gkd_app_release_state state);
#endif
