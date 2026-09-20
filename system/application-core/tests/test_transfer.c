/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-transfer.h"
#include "gkd-app-watch.h"
#include "../source/gkd-app-credentials.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>

static const unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static int fd_count(void)
{
    int n = 0;
    DIR *dir = opendir("/proc/self/fd");
    assert(dir);
    while (readdir(dir)) ++n;
    closedir(dir);
    return n;
}
static void packet(int fd, int right, unsigned count, const char *kind)
{
    unsigned char data[25] = "GKDPID1";
    union { struct cmsghdr alignment; unsigned char bytes[CMSG_SPACE(32 * sizeof(int))]; } control;
    struct iovec iov = {data, 24};
    struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1 };
    memcpy(data + 8, token, 16);
    if (!strcmp(kind, "token")) data[8] ^= 1;
    if (!strcmp(kind, "magic")) data[0] ^= 1;
    if (!strcmp(kind, "short")) iov.iov_len = 23;
    if (!strcmp(kind, "oversize")) iov.iov_len = 25;
    if (!strcmp(kind, "zero")) iov.iov_len = 0;
    if (count) {
        m.msg_control = control.bytes;
        m.msg_controllen = CMSG_SPACE(count * sizeof(int));
        memset(&control, 0, sizeof(control));
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(count * sizeof(int));
        for (unsigned i = 0; i < count; ++i)
            memcpy((unsigned char *)CMSG_DATA(c) + i * sizeof(int), &right, sizeof(right));
    }
    assert(sendmsg(fd, &m, MSG_NOSIGNAL) == (ssize_t)iov.iov_len);
}

static void receiver(const char *kind)
{
    int start = fd_count(), pair[2], status;
    assert(gkd_app_watch_error_channel(pair) == 0);
    int init = (int)syscall(SYS_pidfd_open, getpid(), 0);
    int ordinary = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(init >= 3 && ordinary >= 3);
    struct gkd_app_transfer rx = GKD_APP_TRANSFER_INIT;
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(),
        getuid() + (!strcmp(kind, "uid")), token, 100, 500) == 0);
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(), getuid(), token, 100, 500) == -1);
    assert(errno == EBUSY);
    enum gkd_app_transfer_state want = GKD_TRANSFER_BAD_EVENT;
    uint64_t now = 101;
    if (!strcmp(kind, "eof")) {
        close(pair[1]); pair[1] = -1; want = GKD_TRANSFER_CANCELLED;
    } else if (!strcmp(kind, "deadline") || !strcmp(kind, "backwards")) {
        now = !strcmp(kind, "deadline") ? 600 : 99;
        want = GKD_TRANSFER_TIMED_OUT;
    } else if (!strcmp(kind, "fork")) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            assert(gkd_app_transfer_poll(&rx, 101) == GKD_TRANSFER_CHANNEL_ERROR);
            assert(errno == ECHILD);
            pid_t unused;
            assert(gkd_app_transfer_take(&rx, &unused) == -1 && errno == ECHILD);
            _exit(0);
        }
        assert(waitpid(child, &status, 0) == child && status == 0);
        want = GKD_TRANSFER_WAITING;
    } else if (!strcmp(kind, "pid")) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { packet(pair[1], init, 1, kind); _exit(0); }
        assert(waitpid(child, &status, 0) == child && status == 0);
    } else {
        unsigned count = !strcmp(kind, "no-rights") ? 0 :
                         !strcmp(kind, "two-rights") ? 2 :
                         !strcmp(kind, "truncated-rights") ? 32 : 1;
        packet(pair[1], !strcmp(kind, "not-pidfd") ? ordinary : init, count, kind);
    }
    int before = fd_count();
    assert(gkd_app_transfer_poll(&rx, now) == want);
    assert(rx.child_fd == -1 && fd_count() == before);
    if (want != GKD_TRANSFER_WAITING)
        assert(gkd_app_transfer_poll(&rx, 1000) == want);
    pid_t unchanged = -123;
    assert(gkd_app_transfer_take(&rx, &unchanged) == -1 && unchanged == -123);
    gkd_app_transfer_close(&rx);
    assert(fcntl(pair[0], F_GETFD) >= 0 && fcntl(init, F_GETFD) >= 0);
    close(pair[0]); if (pair[1] >= 0) close(pair[1]);
    close(init); close(ordinary);
    assert(fd_count() == start);
    printf("transfer receiver %s PASS\n", kind);
}

static void sender(const char *kind)
{
    int start = fd_count(), pair[2], block[2], status;
    assert(gkd_app_watch_error_channel(pair) == 0 && pipe2(block, O_CLOEXEC) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(block[1]);
        close(pair[0]); close(pair[1]);
        char value;
        assert(read(block[0], &value, 1) >= 0);
        _exit(0);
    }
    close(block[0]);
    int app = (int)syscall(SYS_pidfd_open, child, 0);
    int self = (int)syscall(SYS_pidfd_open, getpid(), 0);
    assert(app >= 3 && self >= 3);
    struct gkd_app_transfer_tx tx = GKD_APP_TRANSFER_TX_INIT;
    if (!strcmp(kind, "mismatch")) {
        assert(gkd_app_transfer_send(&tx, pair[1], app, getpid(), token) == -1);
        assert(errno == EINVAL && !tx.sent);
    } else if (!strcmp(kind, "nonchild")) {
        assert(gkd_app_transfer_send(&tx, pair[1], self, getpid(), token) == -1);
        assert(errno == ECHILD && !tx.sent);
    } else if (!strcmp(kind, "shutdown")) {
        assert(shutdown(pair[1], SHUT_WR) == 0);
        assert(gkd_app_transfer_send(&tx, pair[1], app, child, token) == -1);
        assert(errno == EPIPE && !tx.sent); /* SIGPIPE has its default disposition. */
    } else {
        if (!strcmp(kind, "queue")) {
            unsigned char data[32] = {0};
            while (send(pair[1], data, sizeof(data), MSG_DONTWAIT | MSG_NOSIGNAL) >= 0) {}
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            assert(gkd_app_transfer_send(&tx, pair[1], app, child, token) == -1);
            assert((errno == EAGAIN || errno == EWOULDBLOCK) && !tx.sent);
            while (recv(pair[0], data, sizeof(data), MSG_DONTWAIT) >= 0) {}
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
        }
        assert(gkd_app_transfer_send(&tx, pair[1], app, child, token) == 0 && tx.sent);
        assert(gkd_app_transfer_send(&tx, pair[1], app, child, token) == -1 && errno == EALREADY);
        unsigned char data[24];
        union { struct cmsghdr alignment;
            unsigned char bytes[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int))]; } control;
        struct iovec iov = {data, sizeof(data)};
        struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
            .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes) };
        assert(recvmsg(pair[0], &message, MSG_CMSG_CLOEXEC) == 24);
        int received = -1;
        assert(gkd_app_receive_credentials(&message, getpid(), getuid(), &received, 1));
        assert(!memcmp(data, "GKDPID1\0", 8) && !memcmp(data + 8, token, 16));
        assert(received >= 3 && (fcntl(received, F_GETFD) & FD_CLOEXEC));
        assert(syscall(SYS_pidfd_send_signal, received, 0, NULL, 0U) == 0);
        close(received);
    }
    assert(fcntl(app, F_GETFD) >= 0); /* Sender borrows. */
    close(block[1]);
    assert(waitpid(child, &status, 0) == child && status == 0);
    close(pair[0]); close(pair[1]); close(app); close(self);
    assert(fd_count() == start);
    printf("transfer sender %s PASS\n", kind);
}

int main(void)
{
    const char *cases[] = {"token", "magic", "short", "oversize", "zero",
        "no-rights", "two-rights", "truncated-rights", "uid", "pid", "not-pidfd",
        "init-as-app", "eof", "deadline", "backwards", "fork"};
    assert(signal(SIGPIPE, SIG_DFL) != SIG_ERR);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) receiver(cases[i]);
    const char *sends[] = {"valid-duplicate", "mismatch", "nonchild", "shutdown", "queue"};
    for (unsigned i = 0; i < sizeof(sends) / sizeof(sends[0]); ++i) sender(sends[i]);
    int pair[2], init = (int)syscall(SYS_pidfd_open, getpid(), 0);
    assert(gkd_app_watch_error_channel(pair) == 0);
    struct gkd_app_transfer rx = GKD_APP_TRANSFER_INIT;
    unsigned char zero[16] = {0};
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(), getuid(), zero, 100, 1) == -1);
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(), getuid(), token, UINT64_MAX, 1) == -1);
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(), getuid(), token, 100, 0) == -1);
    assert(fcntl(pair[0], F_SETFL, 0) == 0);
    assert(gkd_app_transfer_init(&rx, pair[0], init, getpid(), getuid(), token, 100, 1) == -1);
    close(pair[0]); close(pair[1]); close(init);
    puts("GKD_APP_TRANSFER=PASS cases=21 auxiliary-invalid-arguments=PASS");
    return 0;
}
