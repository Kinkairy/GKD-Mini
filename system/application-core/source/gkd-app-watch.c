/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-watch.h"
#include "gkd-app-exec.h"
#include "gkd-app-credentials.h"
#include "gkd-app-pidfd.h"
#include "gkd-app-error-receiver.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/random.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

int gkd_app_launch_token(unsigned char token[16])
{
    unsigned char fresh[16];
    unsigned int any = 0;
    ssize_t count;
    if (!token) { errno = EINVAL; return -1; }
    memset(token, 0, 16);
    count = syscall(SYS_getrandom, fresh, sizeof(fresh), GRND_NONBLOCK);
    if (count < 0) return -1;
    if (count != (ssize_t)sizeof(fresh)) { errno = EIO; return -1; }
    for (size_t i = 0; i < sizeof(fresh); ++i) any |= fresh[i];
    if (!any) { errno = EIO; return -1; }
    memcpy(token, fresh, sizeof(fresh));
    return 0;
}

int gkd_app_watch_error_channel(int pair[2])
{
    int yes = 1, saved;
    if (!pair) { errno = EINVAL; return -1; }
    pair[0] = pair[1] = -1;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair)) return -1;
    if (!setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &yes, sizeof(yes))) return 0;
    saved = errno;
    (void)close(pair[0]); (void)close(pair[1]);
    pair[0] = pair[1] = -1;
    errno = saved;
    return -1;
}



int gkd_app_watch_init(struct gkd_app_watch *watch, int pidfd, int error_fd,
                       int ready_fd, pid_t pid, uid_t uid, const unsigned char token[16],
                       uint64_t now_ms, uint64_t timeout_ms)
{
    struct gkd_app_ready_gate ready = GKD_APP_READY_GATE_INIT;
    if (!watch || pid <= 0 || pidfd < 3 || error_fd < 3 || ready_fd < 3 ||
        pidfd == error_fd || pidfd == ready_fd || error_fd == ready_fd) { errno = EINVAL; return -1; }
    if (watch->pidfd >= 0 || watch->error_fd >= 0 || watch->ready.fd >= 0) { errno = EBUSY; return -1; }
    if (gkd_app_seqpacket_endpoint(error_fd, 1) || gkd_app_pidfd_match(pidfd, pid)) return -1;
    if (gkd_app_ready_init(&ready, ready_fd, pid, uid, token, now_ms, timeout_ms)) return -1;
    memset(watch, 0, sizeof(*watch));
    watch->ready = ready; watch->pidfd = pidfd; watch->error_fd = error_fd;
    watch->last_ms = now_ms; watch->state = GKD_WATCH_WAITING;
    return 0;
}

static int receive_error(struct gkd_app_watch *watch)
{
    unsigned char packet[GKD_APP_EXEC_ERROR_BYTES];
    union { struct cmsghdr alignment;
        unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(4*sizeof(int))];
    } control;
    struct iovec vector = {packet, sizeof(packet)};
    struct msghdr message;
    ssize_t n;
    int valid;
    memset(&message, 0, sizeof(message)); memset(&control, 0, sizeof(control));
    message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = control.bytes; message.msg_controllen = sizeof(control.bytes);
    n = recvmsg(watch->error_fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    valid = gkd_app_check_credentials(&message, watch->ready.pid, watch->ready.uid);
    /* A zero-byte packet carries credentials; a genuine peer EOF has no cmsg. */
    if (!n && !message.msg_controllen && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
        watch->error_eof = 1;
        (void)close(watch->error_fd); watch->error_fd = -1;
        return 0;
    }
    if (!valid || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        gkd_app_exec_error_decode(packet, (size_t)n, &watch->exec_errno)) return -2;
    return 1;
}

enum gkd_app_watch_state gkd_app_watch_poll(struct gkd_app_watch *watch, uint64_t now_ms)
{
    int error, live;
    enum gkd_app_ready_state frame;
    if (!watch || watch->pidfd < 0) return GKD_WATCH_CHANNEL_ERROR;
    if (watch->state != GKD_WATCH_WAITING && watch->state != GKD_WATCH_FRAME_SUBMITTED)
        return watch->state;
    if (now_ms < watch->last_ms) return watch->state = GKD_WATCH_CHANNEL_ERROR;
    watch->last_ms = now_ms;
    /* Retain an authenticated exec errno even if the child has already exited. */
    if (watch->error_fd >= 0) {
        error = receive_error(watch);
        if (error == 1) return watch->state = GKD_WATCH_EXEC_FAILED;
        if (error == -2) return watch->state = GKD_WATCH_BAD_EVENT;
        if (error < 0) return watch->state = GKD_WATCH_CHANNEL_ERROR;
    }
    live = gkd_app_pidfd_alive(watch->pidfd);
    if (live < 0) return watch->state = GKD_WATCH_CHANNEL_ERROR;
    if (!live) {
        (void)gkd_app_ready_poll(&watch->ready, now_ms, 0);
        return watch->state = GKD_WATCH_CHILD_EXITED;
    }
    if (watch->state == GKD_WATCH_WAITING && now_ms >= watch->ready.deadline_ms)
        return watch->state = GKD_WATCH_TIMED_OUT;
    frame = gkd_app_ready_poll(&watch->ready, now_ms, 1);
    switch (frame) {
    case GKD_APP_WAITING: return watch->state;
    case GKD_APP_FRAME_SUBMITTED:
        if (watch->error_eof) watch->state = GKD_WATCH_FRAME_SUBMITTED;
        return watch->state;
    case GKD_APP_TIMED_OUT: return watch->state = GKD_WATCH_TIMED_OUT;
    case GKD_APP_CHILD_EXITED: return watch->state = GKD_WATCH_CHILD_EXITED;
    case GKD_APP_BAD_EVENT: return watch->state = GKD_WATCH_BAD_EVENT;
    default: return watch->state = GKD_WATCH_CHANNEL_ERROR;
    }
}

void gkd_app_watch_close(struct gkd_app_watch *watch)
{
    if (!watch) return;
    gkd_app_ready_close(&watch->ready);
    if (watch->pidfd >= 0) (void)close(watch->pidfd);
    if (watch->error_fd >= 0) (void)close(watch->error_fd);
    memset(watch, 0, sizeof(*watch));
    watch->ready.fd = watch->pidfd = watch->error_fd = -1;
    watch->ready.state = GKD_APP_CHANNEL_ERROR;
    watch->state = GKD_WATCH_CHANNEL_ERROR;
}

const char *gkd_app_watch_name(enum gkd_app_watch_state state)
{
    switch (state) {
    case GKD_WATCH_WAITING: return "waiting";
    case GKD_WATCH_FRAME_SUBMITTED: return "frame-submitted";
    case GKD_WATCH_EXEC_FAILED: return "exec-failed";
    case GKD_WATCH_CHILD_EXITED: return "child-exited";
    case GKD_WATCH_TIMED_OUT: return "timed-out";
    case GKD_WATCH_BAD_EVENT: return "bad-event";
    default: return "channel-error";
    }
}
