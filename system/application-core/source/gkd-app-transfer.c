/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-transfer.h"
#include "gkd-app-credentials.h"
#include "gkd-app-error-receiver.h"
#include "gkd-app-pidfd.h"
#include <sys/stat.h>

static const unsigned char magic[8] = {'G','K','D','P','I','D','1',0};
struct identity { pid_t parent, first, last; unsigned count; };

static int token_valid(const unsigned char *token)
{
    unsigned value = 0;
    if (!token) return 0;
    for (unsigned i = 0; i < 16; ++i) value |= token[i];
    return value != 0;
}

/* Strict bounded kernel fields; do not depend on native libc C23 number APIs. */
static int numbers(char *p, pid_t *first, pid_t *last, unsigned *count)
{
    *count = 0;
    while (*p == '\t') {
        unsigned value = 0;
        char *start = ++p;
        while (*p >= '0' && *p <= '9') {
            unsigned digit = (unsigned)(*p++ - '0');
            if (value > ((unsigned)INT_MAX - digit) / 10U) return -1;
            value = value * 10U + digit;
        }
        if (p == start || ++*count > 33) return -1;
        if (*count == 1) *first = (pid_t)value;
        *last = (pid_t)value;
    }
    return *p == '\n' && *count ? 0 : -1;
}

static int identity(pid_t pid, struct identity *result)
{
    char path[64], contents[8192], *line;
    size_t used = 0;
    int seen_parent = 0, seen_ns = 0, saved = EPROTO;
    (void)snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    for (;;) {
        ssize_t n = read(fd, contents + used, sizeof(contents) - 1 - used);
        if (n < 0) { saved = errno; goto fail; }
        if (!n) break;
        used += (size_t)n;
        if (used == sizeof(contents) - 1) { saved = EOVERFLOW; goto fail; }
    }
    (void)close(fd);
    contents[used] = 0;
    for (line = contents; line && *line; ) {
        pid_t first, last;
        unsigned count;
        if (!strncmp(line, "PPid:", 5)) {
            if (++seen_parent != 1 || numbers(line + 5, &first, &last, &count) || count != 1)
                goto invalid;
            result->parent = first;
        } else if (!strncmp(line, "NSpid:", 6)) {
            if (++seen_ns != 1 ||
                numbers(line + 6, &result->first, &result->last, &result->count))
                goto invalid;
        }
        line = strchr(line, '\n');
        if (line) ++line;
    }
    if (seen_parent == 1 && seen_ns == 1 && result->first == pid && result->last > 0)
        return 0;
invalid:
    errno = EPROTO;
    return -1;
fail:
    (void)close(fd);
    errno = saved;
    return -1;
}

static int same_object(pid_t left, pid_t right, const char *suffix)
{
    char path[80];
    struct stat a, b;
    (void)snprintf(path, sizeof(path), "/proc/%ld/%s", (long)left, suffix);
    if (stat(path, &a)) return -1;
    (void)snprintf(path, sizeof(path), "/proc/%ld/%s", (long)right, suffix);
    if (stat(path, &b)) return -1;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

static int child_identity(int init_fd, pid_t init_pid, int child_fd, pid_t child_pid)
{
    struct identity init, app;
    if (gkd_app_pidfd_match(init_fd, init_pid) ||
        gkd_app_pidfd_match(child_fd, child_pid)) return -1;
    if (identity(init_pid, &init) || identity(child_pid, &app)) return -1;
    if (init_pid == child_pid || init.last != 1 || app.last <= 1 ||
        app.parent != init_pid || init.count != app.count) goto invalid;
    const char *objects[] = {"ns/pid", "ns/mnt", "ns/user", "root"};
    for (unsigned i = 0; i < sizeof(objects) / sizeof(objects[0]); ++i) {
        int same = same_object(init_pid, child_pid, objects[i]);
        if (same < 0) return -1;
        if (!same) goto invalid;
    }
    for (unsigned i = 0; i < 2; ++i) {
        int same = same_object(init_pid, getpid(), objects[i]);
        if (same < 0) return -1;
        if (same) goto invalid;
    }
    if (gkd_app_pidfd_match(init_fd, init_pid) ||
        gkd_app_pidfd_match(child_fd, child_pid)) return -1;
    return 0;
invalid:
    errno = EPROTO;
    return -1;
}

int gkd_app_transfer_init(struct gkd_app_transfer *rx, int fd, int init_fd,
    pid_t init_pid, uid_t init_uid, const unsigned char token[16],
    uint64_t now_ms, uint64_t timeout_ms)
{
    if (!rx || rx->child_fd != -1 || fd < 3 || init_fd < 3 || fd == init_fd ||
        init_pid <= 0 || !token_valid(token) || !timeout_ms ||
        UINT64_MAX - now_ms < timeout_ms) { errno = EINVAL; return -1; }
    if (rx->fd >= 0) { errno = EBUSY; return -1; }
    if (gkd_app_seqpacket_endpoint(fd, 1) || gkd_app_pidfd_match(init_fd, init_pid))
        return -1;
    *rx = (struct gkd_app_transfer) { .fd = fd, .init_fd = init_fd, .child_fd = -1,
        .owner = getpid(), .init_pid = init_pid, .init_uid = init_uid,
        .last_ms = now_ms, .deadline_ms = now_ms + timeout_ms, .state = GKD_TRANSFER_WAITING };
    memcpy(rx->token, token, 16);
    return 0;
}

enum gkd_app_transfer_state gkd_app_transfer_poll(struct gkd_app_transfer *rx, uint64_t now_ms)
{
    unsigned char packet[24];
    union { struct cmsghdr alignment; unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) +
        CMSG_SPACE(8 * sizeof(int))]; } ancillary;
    struct iovec iov = {packet, sizeof(packet)};
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = ancillary.bytes, .msg_controllen = sizeof(ancillary.bytes) };
    if (!rx || rx->owner != getpid()) { errno = ECHILD; return GKD_TRANSFER_CHANNEL_ERROR; }
    if (rx->state != GKD_TRANSFER_WAITING) return rx->state;
    if (now_ms < rx->last_ms || now_ms >= rx->deadline_ms)
        return rx->state = GKD_TRANSFER_TIMED_OUT;
    rx->last_ms = now_ms;
    ssize_t n = recvmsg(rx->fd, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return rx->state;
        return rx->state = GKD_TRANSFER_CHANNEL_ERROR;
    }
    if (!n && !message.msg_controllen && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
        return rx->state = GKD_TRANSFER_CANCELLED;
    int received = -1;
    int valid = gkd_app_receive_credentials(&message, rx->init_pid, rx->init_uid, &received, 1);
    if (!valid || n != sizeof(packet) || memcmp(packet, magic, 8) ||
        memcmp(packet + 8, rx->token, 16)) goto bad;
    if (received < 3 || gkd_app_pidfd_pid(received, &rx->child_pid) ||
        child_identity(rx->init_fd, rx->init_pid, received, rx->child_pid)) goto bad;
    rx->child_fd = received;
    return rx->state = GKD_TRANSFER_RECEIVED;
bad:
    if (received >= 0) (void)close(received);
    return rx->state = GKD_TRANSFER_BAD_EVENT;
}

int gkd_app_transfer_take(struct gkd_app_transfer *rx, pid_t *child_pid)
{
    if (!rx || rx->owner != getpid()) { errno = ECHILD; return -1; }
    if (!child_pid || rx->state != GKD_TRANSFER_RECEIVED) { errno = EINVAL; return -1; }
    if (child_identity(rx->init_fd, rx->init_pid, rx->child_fd, rx->child_pid)) return -1;
    int fd = rx->child_fd;
    *child_pid = rx->child_pid;
    rx->child_fd = -1;
    rx->state = GKD_TRANSFER_TAKEN;
    return fd;
}
void gkd_app_transfer_close(struct gkd_app_transfer *rx)
{
    if (!rx) return;
    if (rx->child_fd >= 0) (void)close(rx->child_fd);
    *rx = (struct gkd_app_transfer)GKD_APP_TRANSFER_INIT;
}
const char *gkd_app_transfer_name(enum gkd_app_transfer_state state)
{
    static const char *names[] = {"waiting", "received", "cancelled", "timed-out",
        "bad-event", "channel-error", "taken"};
    return state >= 0 && state <= GKD_TRANSFER_TAKEN ? names[state] : "invalid";
}

int gkd_app_transfer_send(struct gkd_app_transfer_tx *tx, int fd, int child_fd,
    pid_t child_pid, const unsigned char token[16])
{
    unsigned char packet[24];
    union { struct cmsghdr alignment; unsigned char bytes[CMSG_SPACE(sizeof(int))]; } ancillary;
    struct iovec iov = {packet, sizeof(packet)};
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = ancillary.bytes, .msg_controllen = sizeof(ancillary.bytes) };
    struct identity app;
    if (!tx || fd < 3 || child_fd < 3 || fd == child_fd ||
        child_pid <= 0 || !token_valid(token)) { errno = EINVAL; return -1; }
    if (tx->sent) { errno = EALREADY; return -1; }
    if (gkd_app_seqpacket_endpoint(fd, 0) || gkd_app_pidfd_match(child_fd, child_pid) ||
        identity(child_pid, &app)) return -1;
    if (app.parent != getpid()) { errno = ECHILD; return -1; }
    memcpy(packet, magic, 8);
    memcpy(packet + 8, token, 16);
    memset(&ancillary, 0, sizeof(ancillary));
    struct cmsghdr *item = CMSG_FIRSTHDR(&message);
    item->cmsg_level = SOL_SOCKET;
    item->cmsg_type = SCM_RIGHTS;
    item->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(item), &child_fd, sizeof(child_fd));
    ssize_t n = sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n < 0) return -1;
    if (n != sizeof(packet)) { errno = EIO; return -1; }
    tx->sent = 1;
    return 0;
}
