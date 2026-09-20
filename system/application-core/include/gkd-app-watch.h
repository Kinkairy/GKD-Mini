/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_WATCH_H
#define GKD_APP_WATCH_H
#include "gkd-app-ready.h"

enum gkd_app_watch_state {
    GKD_WATCH_WAITING = 0, GKD_WATCH_FRAME_SUBMITTED, GKD_WATCH_EXEC_FAILED,
    GKD_WATCH_CHILD_EXITED, GKD_WATCH_TIMED_OUT, GKD_WATCH_BAD_EVENT,
    GKD_WATCH_CHANNEL_ERROR
};
struct gkd_app_watch {
    struct gkd_app_ready_gate ready;
    int pidfd, error_fd, exec_errno, error_eof;
    uint64_t last_ms;
    enum gkd_app_watch_state state;
};
#define GKD_APP_WATCH_INIT { .ready = GKD_APP_READY_GATE_INIT, .pidfd = -1, \
    .error_fd = -1, .state = GKD_WATCH_CHANNEL_ERROR }

/* One getrandom NONBLOCK attempt, before fork. Zero output on failure, no
 * fallback or retry policy. A failed/short/all-zero result must block launch.
 */
int gkd_app_launch_token(unsigned char token[GKD_APP_READY_TOKEN_BYTES]);
/* Anonymous error pair: receiver [0] has SO_PASSCRED, both NONBLOCK/CLOEXEC. */
int gkd_app_watch_error_channel(int pair[2]);

/* Controller-side observer, not a launcher/init replacement. Caller verifies
 * preparation sender/namespace/root provenance, pins its unreaped direct child,
 * and arms this watch BEFORE releasing the child. /proc must be mounted for the
 * controller's PID namespace. Checks the pidfd's type, mapped PID and liveness.
 * Only on success takes ownership of all three distinct descriptors >=3.
 * Use GKD_APP_WATCH_INIT; close before reuse. Timeout comes from config-core.
 */
int gkd_app_watch_init(struct gkd_app_watch *watch, int pidfd, int error_fd,
                       int ready_fd, pid_t pid, uid_t uid,
                       const unsigned char token[GKD_APP_READY_TOKEN_BYTES],
                       uint64_t now_ms, uint64_t timeout_ms);
/* One nonblocking observation: exact-child error receipt, pidfd death, deadline
 * and existing readiness parser. Frame evidence requires error EOF too; EOF
 * alone is never success. No waitpid/reaping, signal delivery, sleep or reboot.
 * Any terminal failure sticks. A submitted frame is revoked on observed death.
 * Caller must not close/replace owned descriptors or concurrently access watch.
 */
enum gkd_app_watch_state gkd_app_watch_poll(struct gkd_app_watch *watch, uint64_t now_ms);
void gkd_app_watch_close(struct gkd_app_watch *watch);
const char *gkd_app_watch_name(enum gkd_app_watch_state state);
#endif
