/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line=%d: %s errno=%d\n", __LINE__, #x, errno); exit(1); } } while (0)
static const unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static unsigned int cases;

static void setup(struct gkd_app_ready_gate *gate, int pair[2])
{
    CHECK(gkd_app_ready_channel(pair) == 0);
    CHECK((fcntl(pair[0], F_GETFD) & FD_CLOEXEC) && (fcntl(pair[1], F_GETFD) & FD_CLOEXEC));
    CHECK((fcntl(pair[0], F_GETFL) & O_NONBLOCK) && (fcntl(pair[1], F_GETFL) & O_NONBLOCK));
    CHECK(gkd_app_ready_init(gate, pair[0], getpid(), getuid(), token, 100, 100) == 0);
}
static void teardown(struct gkd_app_ready_gate *gate, int pair[2])
{
    gkd_app_ready_close(gate);
    CHECK(gate->fd == -1 && gate->state == GKD_APP_CHANNEL_ERROR);
    CHECK(close(pair[1]) == 0);
    ++cases;
}
static int fd_count(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    CHECK(dir != NULL);
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    CHECK(closedir(dir) == 0);
    return count;
}
static void packet(unsigned char *bytes, size_t size)
{
    memset(bytes, 0, size);
    memcpy(bytes, "GKDAPR1", 7);
    memcpy(bytes + 8, token, sizeof(token));
}
static void test_lifecycle(void)
{
    int pair[2];
    struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
    setup(&gate, pair);
    CHECK(gkd_app_ready_poll(&gate, 100, 1) == GKD_APP_WAITING);
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_FRAME_SUBMITTED);
    CHECK(gkd_app_ready_poll(&gate, 10000, 1) == GKD_APP_FRAME_SUBMITTED);
    CHECK(gkd_app_ready_poll(&gate, 10001, 0) == GKD_APP_CHILD_EXITED);
    CHECK(gkd_app_ready_poll(&gate, 10002, 1) == GKD_APP_CHILD_EXITED);
    teardown(&gate, pair);
    setup(&gate, pair);
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 200, 1) == GKD_APP_TIMED_OUT);
    CHECK(gkd_app_ready_poll(&gate, 201, 1) == GKD_APP_TIMED_OUT);
    teardown(&gate, pair);
    setup(&gate, pair);
    CHECK(gkd_app_ready_poll(&gate, 101, 0) == GKD_APP_CHILD_EXITED);
    teardown(&gate, pair);
    setup(&gate, pair);
    CHECK(gkd_app_ready_poll(&gate, 99, 1) == GKD_APP_CHANNEL_ERROR);
    teardown(&gate, pair);
}
static void test_identity(void)
{
    int pair[2];
    unsigned char wrong[16];
    struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
    memcpy(wrong, token, sizeof(wrong)); wrong[15] ^= 1;
    setup(&gate, pair);
    CHECK(gkd_app_ready_send(pair[1], wrong) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
    teardown(&gate, pair);
    setup(&gate, pair);
    gate.pid++;
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
    teardown(&gate, pair);
    setup(&gate, pair);
    gate.uid ^= 1;
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
    teardown(&gate, pair);
    setup(&gate, pair);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(gkd_app_ready_send(pair[1], token) == 0 ? 0 : 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
    teardown(&gate, pair);
    setup(&gate, pair);
    int disabled = 0;
    CHECK(setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &disabled, sizeof(disabled)) == 0);
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
    teardown(&gate, pair);
}
static void test_packets(void)
{
    for (unsigned int kind = 0; kind < 4; ++kind) {
        int pair[2];
        unsigned char body[25];
        struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
        setup(&gate, pair); packet(body, sizeof(body));
        size_t size = kind == 0 ? 0 : kind == 1 ? 23 : kind == 2 ? 25 : 24;
        if (kind == 3) body[0] ^= 1;
        CHECK(send(pair[1], body, size, MSG_NOSIGNAL) == (ssize_t)size);
        CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
        teardown(&gate, pair);
    }
    for (unsigned int count = 1; count <= 16; count *= 16) {
        int pair[2], rights[16];
        unsigned char body[24];
        union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(rights))]; } ancillary;
        struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
        setup(&gate, pair); packet(body, sizeof(body));
        int source = open("/dev/null", O_RDONLY | O_CLOEXEC);
        CHECK(source >= 0);
        for (unsigned int i = 0; i < count; ++i) rights[i] = source;
        memset(&ancillary, 0, sizeof(ancillary));
        struct iovec iov = {body, sizeof(body)};
        struct msghdr message = {0};
        message.msg_iov = &iov; message.msg_iovlen = 1;
        message.msg_control = ancillary.bytes; message.msg_controllen = CMSG_SPACE(count * sizeof(int));
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(count * sizeof(int));
        memcpy(CMSG_DATA(header), rights, count * sizeof(int));
        int before = fd_count();
        CHECK(sendmsg(pair[1], &message, MSG_NOSIGNAL) == (ssize_t)sizeof(body));
        CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
        CHECK(fd_count() == before);
        CHECK(close(source) == 0);
        teardown(&gate, pair);
    }
}
static void test_child_success(void)
{
    int pair[2], status;
    struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
    CHECK(gkd_app_ready_channel(pair) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        (void)close(pair[0]);
        _exit(gkd_app_ready_send(pair[1], token) == 0 ? 0 : 1);
    }
    CHECK(gkd_app_ready_init(&gate, pair[0], child, getuid(), token, 100, 100) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    /* Exercise real child credentials separately from lifecycle observation.
     * Production MUST pass false for a reaped child; the next case checks that.
     */
    CHECK(gkd_app_ready_poll(&gate, 199, 1) == GKD_APP_FRAME_SUBMITTED);
    CHECK(gkd_app_ready_poll(&gate, 199, 0) == GKD_APP_CHILD_EXITED);
    teardown(&gate, pair);
    setup(&gate, pair);
    CHECK(gkd_app_ready_send(pair[1], token) == 0);
    CHECK(gkd_app_ready_poll(&gate, 101, 0) == GKD_APP_CHILD_EXITED);
    teardown(&gate, pair);
}
static void test_contract(void)
{
    int pair[2];
    unsigned char zero[16] = {0};
    struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
    CHECK(gkd_app_ready_channel(pair) == 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], getpid(), getuid(), token, UINT64_MAX, 1) < 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], getpid(), getuid(), token, 0, 0) < 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], 0, getuid(), token, 0, 1) < 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], getpid(), getuid(), zero, 0, 1) < 0);
    CHECK(gate.fd == -1 && fcntl(pair[0], F_GETFD) >= 0);
    CHECK(gkd_app_ready_send(pair[1], zero) < 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], getpid(), getuid(), token, 0, 1) == 0);
    CHECK(gkd_app_ready_init(&gate, pair[0], getpid(), getuid(), token, 0, 1) < 0 && errno == EBUSY);
    teardown(&gate, pair);
    int regular = open("/dev/null", O_WRONLY | O_CLOEXEC);
    CHECK(regular >= 0 && gkd_app_ready_send(regular, token) < 0);
    CHECK(close(regular) == 0); ++cases;
    CHECK(gkd_app_ready_channel(pair) == 0);
    CHECK(close(pair[0]) == 0);
    CHECK(gkd_app_ready_send(pair[1], token) < 0);
    CHECK(close(pair[1]) == 0); ++cases;
    CHECK(gkd_app_ready_channel(pair) == 0);
    unsigned int queued;
    for (queued = 0; queued < 100000 && gkd_app_ready_send(pair[1], token) == 0; ++queued) {}
    CHECK(queued < 100000 && (errno == EAGAIN || errno == EWOULDBLOCK));
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0); ++cases;
}
int main(void)
{
    int before = fd_count();
    test_lifecycle(); test_identity(); test_packets(); test_child_success(); test_contract();
    CHECK(fd_count() == before);
    CHECK(!strcmp(gkd_app_ready_name(GKD_APP_FRAME_SUBMITTED), "frame-submitted"));
    CHECK(!strcmp(gkd_app_ready_name((enum gkd_app_ready_state)99), "channel-error"));
    printf("GKD_APP_READY=PASS cases=%u descriptor-leaks=0\n", cases);
    return 0;
}
