/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_READY_H
#define GKD_APP_READY_H

#include <stdint.h>
#include <sys/types.h>

#define GKD_APP_READY_TOKEN_BYTES 16U
#define GKD_APP_READY_PACKET_BYTES 24U

enum gkd_app_ready_state {
    GKD_APP_WAITING = 0,
    GKD_APP_FRAME_SUBMITTED,
    GKD_APP_TIMED_OUT,
    GKD_APP_CHILD_EXITED,
    GKD_APP_BAD_EVENT,
    GKD_APP_CHANNEL_ERROR
};

struct gkd_app_ready_gate {
    int fd;
    pid_t pid;
    uid_t uid;
    unsigned char token[GKD_APP_READY_TOKEN_BYTES];
    uint64_t deadline_ms;
    uint64_t last_ms;
    enum gkd_app_ready_state state;
};
#define GKD_APP_READY_GATE_INIT { .fd = -1, .state = GKD_APP_CHANNEL_ERROR }

/* Anonymous AF_UNIX datagram endpoints; receiver is pair[0]. Both are CLOEXEC
 * and nonblocking. The launcher explicitly passes only pair[1] to the target.
 * This binds a trusted child, not a sandbox against privileged root processes.
 */
int gkd_app_ready_channel(int pair[2]);

/* Initialize gate with GKD_APP_READY_GATE_INIT. Takes ownership of fd only on
 * success; close a prior gate before reinitializing. Token is fresh per launch.
 * Caller owns process identity/start-time verification and supplies monotonic
 * time and configured timeout; this module has no second config parser.
 */
int gkd_app_ready_init(struct gkd_app_ready_gate *gate, int fd, pid_t pid,
                       uid_t uid, const unsigned char token[16],
                       uint64_t now_ms, uint64_t timeout_ms);

/* One bounded, nonblocking datagram. Never writes ordinary files or raises
 * SIGPIPE. Caller decides whether/how to report a transport error.
 */
int gkd_app_ready_send(int fd, const unsigned char token[16]);

/* Poll once; no sleeps and no process/mount/display mutation. child_alive must
 * reflect the SAME verified PID/start-time identity, not pidof or kill(pid,0).
 * Submitted is only frame evidence; death revokes it on the next poll. It is
 * never sufficient by itself for trial-good or proof of physical scanout.
 */
enum gkd_app_ready_state gkd_app_ready_poll(struct gkd_app_ready_gate *gate,
                                          uint64_t now_ms, int child_alive);
void gkd_app_ready_close(struct gkd_app_ready_gate *gate);
const char *gkd_app_ready_name(enum gkd_app_ready_state state);

#endif
