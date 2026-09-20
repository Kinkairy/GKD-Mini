/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-release.h"
#include "gkd-app-init.h"
#include "gkd-app-credentials.h"
#include "gkd-app-error-receiver.h"

static const unsigned char magic[8] = {'G','K','D','R','E','L','1',0};
static int token_valid(const unsigned char *token)
{
    unsigned int any = 0;
    if (!token) return 0;
    for (size_t i = 0; i < 16; ++i) any |= token[i];
    return any != 0;
}

static int endpoint(int fd, int credentials)
{
    struct sockaddr_storage peer;
    socklen_t size = sizeof(peer);
    if (fd < 3) { errno = EINVAL; return -1; }
    if (gkd_app_seqpacket_endpoint(fd, credentials)) return -1;
    if (getpeername(fd, (struct sockaddr *)&peer, &size)) return -1;
    if (peer.ss_family != AF_UNIX || size != sizeof(sa_family_t)) { errno = EINVAL; return -1; }
    return 0;
}

int gkd_app_release_channel(int pair[2])
{
    int enabled = 1, saved;
    if (!pair) { errno = EINVAL; return -1; }
    pair[0] = pair[1] = -1;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair)) return -1;
    if (!setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled))) return 0;
    saved = errno;
    (void)close(pair[0]); (void)close(pair[1]);
    pair[0] = pair[1] = -1;
    errno = saved; return -1;
}

int gkd_app_release_rx_init(struct gkd_app_release_rx *rx, int fd, pid_t sender_pid,
                          uid_t sender_uid, const unsigned char token[16],
                          uint64_t now_ms, uint64_t timeout_ms)
{
    if (!rx || sender_pid < 0 || !token_valid(token) || !timeout_ms ||
        timeout_ms > UINT64_MAX - now_ms) { errno = EINVAL; return -1; }
    if (rx->fd >= 0) { errno = EBUSY; return -1; }
    if (endpoint(fd, 1)) return -1;
    memset(rx, 0, sizeof(*rx));
    rx->fd = fd; rx->owner = getpid(); rx->sender_pid = sender_pid; rx->sender_uid = sender_uid;
    memcpy(rx->token, token, sizeof(rx->token));
    rx->last_ms = now_ms; rx->deadline_ms = now_ms + timeout_ms;
    rx->state = GKD_RELEASE_WAITING;
    return 0;
}

enum gkd_app_release_state gkd_app_release_poll(struct gkd_app_release_rx *rx,
                                               uint64_t now_ms)
{
    unsigned char packet[24];
    union { struct cmsghdr alignment;
        unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(4 * sizeof(int))];
    } control;
    struct iovec vector = {packet, sizeof(packet)};
    struct msghdr message;
    ssize_t count;
    int valid;
    if (!rx || rx->fd < 3) return GKD_RELEASE_CHANNEL_ERROR;
    if (rx->owner != getpid()) { errno = ECHILD; return GKD_RELEASE_CHANNEL_ERROR; }
    if (rx->state != GKD_RELEASE_WAITING) return rx->state;
    if (now_ms < rx->last_ms) return rx->state = GKD_RELEASE_CHANNEL_ERROR;
    rx->last_ms = now_ms;
    if (now_ms >= rx->deadline_ms) return rx->state = GKD_RELEASE_TIMED_OUT;
    memset(&message, 0, sizeof(message)); memset(&control, 0, sizeof(control));
    message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = control.bytes; message.msg_controllen = sizeof(control.bytes);
    count = recvmsg(rx->fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return rx->state;
        return rx->state = GKD_RELEASE_CHANNEL_ERROR;
    }
    valid = gkd_app_check_credentials(&message, rx->sender_pid, rx->sender_uid);
    if (!count && !message.msg_controllen && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
        return rx->state = GKD_RELEASE_CANCELLED;
    if (!valid || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || count != sizeof(packet) ||
        memcmp(packet, magic, sizeof(magic)) || memcmp(packet + sizeof(magic), rx->token, 16))
        return rx->state = GKD_RELEASE_BAD_EVENT;
    return rx->state = GKD_RELEASE_GRANTED;
}

int gkd_app_release_send(struct gkd_app_release_tx *tx, struct gkd_app_watch *watch, int release_fd,
                         int init_pidfd, pid_t init_pid, int init_executable_fd,
                         uint64_t now_ms)
{
    unsigned char packet[24];
    enum gkd_app_watch_state state;
    int matched;
    ssize_t sent;
    if (!tx || !watch || !token_valid(watch->ready.token)) { errno = EINVAL; return -1; }
    if (tx->sent) { errno = EALREADY; return -1; }
    if (watch->pidfd < 3 || watch->ready.fd < 3 || watch->error_fd < 3 ||
        release_fd == watch->pidfd || release_fd == watch->ready.fd || release_fd == watch->error_fd ||
        release_fd == init_pidfd || release_fd == init_executable_fd ||
        init_pidfd == watch->pidfd || init_pidfd == watch->ready.fd || init_pidfd == watch->error_fd ||
        init_executable_fd == watch->pidfd || init_executable_fd == watch->ready.fd ||
        init_executable_fd == watch->error_fd) { errno = EINVAL; return -1; }
    if (endpoint(release_fd, 0)) return -1;
    matched = gkd_app_init_image(init_pidfd, init_pid, init_executable_fd);
    if (matched < 0) return -1;
    state = gkd_app_watch_poll(watch, now_ms);
    if (state != GKD_WATCH_WAITING || watch->ready.state != GKD_APP_WAITING || watch->error_eof) {
        errno = state == GKD_WATCH_TIMED_OUT ? ETIMEDOUT :
                state == GKD_WATCH_CHILD_EXITED ? ESRCH : EPROTO;
        return -1;
    }
    if (!matched) { errno = EAGAIN; return -1; }
    memcpy(packet, magic, sizeof(magic)); memcpy(packet + sizeof(magic), watch->ready.token, 16);
    sent = send(release_fd, packet, sizeof(packet), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == (ssize_t)sizeof(packet)) { tx->sent = 1; return 0; }
    if (sent >= 0) errno = EIO;
    return -1;
}

const char *gkd_app_release_name(enum gkd_app_release_state state)
{
    switch (state) {
    case GKD_RELEASE_WAITING: return "waiting";
    case GKD_RELEASE_GRANTED: return "granted";
    case GKD_RELEASE_CANCELLED: return "cancelled";
    case GKD_RELEASE_TIMED_OUT: return "timed-out";
    case GKD_RELEASE_BAD_EVENT: return "bad-event";
    default: return "channel-error";
    }
}
