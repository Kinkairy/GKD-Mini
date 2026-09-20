/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include "gkd-app-credentials.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const unsigned char magic[8] = {'G','K','D','A','P','R','1',0};

static int valid_token(const unsigned char *token)
{
    unsigned int i, any = 0;
    if (!token) return 0;
    for (i = 0; i < GKD_APP_READY_TOKEN_BYTES; ++i) any |= token[i];
    return any != 0;
}

static int unix_datagram(int fd)
{
    struct sockaddr_storage address;
    socklen_t bytes = sizeof(address), type_bytes = sizeof(int);
    int type;
    if (getsockname(fd, (struct sockaddr *)&address, &bytes) ||
        getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_bytes)) return 0;
    if (address.ss_family != AF_UNIX || type != SOCK_DGRAM) {
        errno = EPROTOTYPE;
        return 0;
    }
    return 1;
}

int gkd_app_ready_channel(int pair[2])
{
    int enabled = 1, saved;
    if (!pair) { errno = EINVAL; return -1; }
    pair[0] = pair[1] = -1;
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair))
        return -1;
    if (!setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)))
        return 0;
    saved = errno;
    (void)close(pair[0]); (void)close(pair[1]);
    pair[0] = pair[1] = -1;
    errno = saved;
    return -1;
}

int gkd_app_ready_init(struct gkd_app_ready_gate *gate, int fd, pid_t pid,
                       uid_t uid, const unsigned char token[16],
                       uint64_t now_ms, uint64_t timeout_ms)
{
    int enabled = 0;
    socklen_t bytes = sizeof(enabled);
    if (!gate || pid <= 0 || !valid_token(token) || !timeout_ms ||
        timeout_ms > UINT64_MAX - now_ms) { errno = EINVAL; return -1; }
    if (gate->fd >= 0) { errno = EBUSY; return -1; }
    if (!unix_datagram(fd)) return -1;
    if (getsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, &bytes)) return -1;
    if (!enabled) { errno = EINVAL; return -1; }
    memset(gate, 0, sizeof(*gate));
    gate->fd = fd; gate->pid = pid; gate->uid = uid;
    memcpy(gate->token, token, sizeof(gate->token));
    gate->last_ms = now_ms;
    gate->deadline_ms = now_ms + timeout_ms;
    gate->state = GKD_APP_WAITING;
    return 0;
}

int gkd_app_ready_send(int fd, const unsigned char token[16])
{
    unsigned char packet[GKD_APP_READY_PACKET_BYTES];
    ssize_t sent;
    if (!valid_token(token)) { errno = EINVAL; return -1; }
    if (!unix_datagram(fd)) return -1;
    memcpy(packet, magic, sizeof(magic));
    memcpy(packet + sizeof(magic), token, GKD_APP_READY_TOKEN_BYTES);
    sent = send(fd, packet, sizeof(packet), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == (ssize_t)sizeof(packet)) return 0;
    if (sent >= 0) errno = EIO;
    return -1;
}

enum gkd_app_ready_state gkd_app_ready_poll(struct gkd_app_ready_gate *gate,
                                          uint64_t now_ms, int child_alive)
{
    unsigned char packet[GKD_APP_READY_PACKET_BYTES];
    union {
        struct cmsghdr alignment;
        unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(4 * sizeof(int))];
    } control;
    struct iovec vector = {packet, sizeof(packet)};
    struct msghdr message;
    ssize_t received;
    int accepted;
    if (!gate || gate->fd < 0) return GKD_APP_CHANNEL_ERROR;
    if (gate->state != GKD_APP_WAITING && gate->state != GKD_APP_FRAME_SUBMITTED)
        return gate->state;
    if (!child_alive) return gate->state = GKD_APP_CHILD_EXITED;
    if (now_ms < gate->last_ms) return gate->state = GKD_APP_CHANNEL_ERROR;
    gate->last_ms = now_ms;
    if (gate->state == GKD_APP_FRAME_SUBMITTED) return gate->state;
    if (now_ms >= gate->deadline_ms) return gate->state = GKD_APP_TIMED_OUT;
    memset(&message, 0, sizeof(message));
    memset(&control, 0, sizeof(control));
    message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = control.bytes; message.msg_controllen = sizeof(control.bytes);
    received = recvmsg(gate->fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return gate->state;
        return gate->state = GKD_APP_CHANNEL_ERROR;
    }
    /* Always inspect control messages before rejecting size/truncation. */
    accepted = gkd_app_check_credentials(&message, gate->pid, gate->uid);
    if (!accepted || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        received != (ssize_t)sizeof(packet) || memcmp(packet, magic, sizeof(magic)) ||
        memcmp(packet + sizeof(magic), gate->token, sizeof(gate->token)))
        return gate->state = GKD_APP_BAD_EVENT;
    return gate->state = GKD_APP_FRAME_SUBMITTED;
}

void gkd_app_ready_close(struct gkd_app_ready_gate *gate)
{
    if (!gate) return;
    if (gate->fd >= 0) (void)close(gate->fd);
    memset(gate, 0, sizeof(*gate));
    gate->fd = -1;
    gate->state = GKD_APP_CHANNEL_ERROR;
}

const char *gkd_app_ready_name(enum gkd_app_ready_state state)
{
    switch (state) {
    case GKD_APP_WAITING: return "waiting";
    case GKD_APP_FRAME_SUBMITTED: return "frame-submitted";
    case GKD_APP_TIMED_OUT: return "timed-out";
    case GKD_APP_CHILD_EXITED: return "child-exited";
    case GKD_APP_BAD_EVENT: return "bad-event";
    default: return "channel-error";
    }
}
