/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-watch.h"
#include "gkd-app-exec.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "watch line=%d errno=%d\n", __LINE__, errno); exit(1); } } while (0)
static int descriptors(void)
{
    DIR *dir = opendir("/proc/self/fd"); struct dirent *entry; int n = 0;
    CHECK(dir);
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++n;
    CHECK(closedir(dir) == 0); return n;
}
static uint64_t milliseconds(void)
{
    struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000 + (unsigned long)t.tv_nsec / 1000000;
}
static void send_error(int fd, unsigned int kind, int ordinary)
{
    unsigned char packet[13] = {'G','K','D','E','X','E','1',0,0,0,0,ENOEXEC,0};
    size_t length = 12;
    if (kind == 8) length = 13;
    if (kind == 9) packet[0] = 'X';
    if (kind == 10) packet[11] = 0;
    if (kind == 13) length = 0;
    if (kind == 7 || kind == 14) {
        int rights[16]; for (size_t i = 0; i < 16; ++i) rights[i] = ordinary;
        size_t count = kind == 7 ? 1 : 16;
        union { struct cmsghdr alignment; unsigned char bytes[CMSG_SPACE(sizeof(rights))]; } control;
        struct iovec vector = {packet, length};
        struct msghdr message; memset(&message, 0, sizeof(message)); memset(&control, 0, sizeof(control));
        message.msg_iov = &vector; message.msg_iovlen = 1;
        message.msg_control = control.bytes; message.msg_controllen = CMSG_SPACE(count * sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(count * sizeof(int));
        memcpy(CMSG_DATA(c), rights, count * sizeof(int));
        CHECK(sendmsg(fd, &message, MSG_NOSIGNAL) == (ssize_t)length);
    } else CHECK(send(fd, packet, length, MSG_NOSIGNAL) == (ssize_t)length);
}

int main(void)
{
    static const char *names[] = {"exec-frame-exit", "actual-ENOEXEC", "validation-EINVAL",
        "exit-without-frame", "wrong-token", "EOF-without-frame", "foreign-error-sender",
        "rights", "oversize", "bad-magic", "zero-errno", "wrong-uid", "backward-clock",
        "zero-packet", "truncated-rights", "frame-without-EOF", "late-error", "error-after-exit"};
    int initial = descriptors();
    unsigned char token[16], other[16], packet[12] = {'G','K','D','E','X','E','1',0,0,0,0,ENOEXEC};
    CHECK(gkd_app_launch_token(NULL) == -1 && errno == EINVAL);
    CHECK(gkd_app_launch_token(token) == 0 && gkd_app_launch_token(other) == 0 && memcmp(token, other, 16));
    int decoded = 123;
    CHECK(gkd_app_exec_error_decode(packet, 12, &decoded) == 0 && decoded == ENOEXEC);
    decoded = 123;
    CHECK(gkd_app_exec_error_decode(packet, 11, &decoded) == -1 && decoded == 123);
    packet[8] = 1;
    CHECK(gkd_app_exec_error_decode(packet, 12, &decoded) == -1 && decoded == 123);
    CHECK(gkd_app_exec_error_decode(NULL, 12, &decoded) == -1 && decoded == 123);
    CHECK(gkd_app_exec_error_decode(packet, 12, NULL) == -1);
    CHECK(gkd_app_watch_error_channel(NULL) == -1 && errno == EINVAL);
    CHECK(gkd_app_watch_poll(NULL, 0) == GKD_WATCH_CHANNEL_ERROR);
    gkd_app_watch_close(NULL);
    int executable = open("/test/exec-fixture", O_RDONLY | O_CLOEXEC);
    int terminal = open("/dev/null", O_RDWR | O_CLOEXEC);
    int output[2]; CHECK(executable >= 3 && terminal >= 3 && pipe2(output, O_NONBLOCK | O_CLOEXEC) == 0);
    for (unsigned int kind = 0; kind < sizeof(names)/sizeof(names[0]); ++kind) {
        int ready[2], errors[2], barrier[2], status;
        struct gkd_app_watch watch = GKD_APP_WATCH_INIT;
        CHECK(gkd_app_ready_channel(ready) == 0 && gkd_app_watch_error_channel(errors) == 0);
        CHECK(pipe2(barrier, O_CLOEXEC) == 0 && gkd_app_launch_token(token) == 0);
        pid_t preparer = getpid(), child = fork(); CHECK(child >= 0);
        if (!child) {
            char go; CHECK(close(barrier[1]) == 0 && close(errors[0]) == 0 && close(ready[0]) == 0);
            CHECK(read(barrier[0], &go, 1) == 1 && close(barrier[0]) == 0);
            if (kind <= 2 || kind == 4) {
                char *argv[] = {"exec-fixture", "--watch-hold", NULL};
                char *environment[] = {"HOME=/fixture-home", "PATH=/bin", NULL};
                char *bad[] = {"LD_PRELOAD=/wrong", NULL};
                struct gkd_app_exec_request request = {preparer, executable, terminal, output[1], ready[1],
                    "/test/libgkd-sm-present.so", token, argv, environment};
                if (kind == 1) {
                    int invalid = memfd_create("watch-bad-elf", MFD_CLOEXEC);
                    CHECK(invalid >= 3 && write(invalid, "\177ELF", 4) == 4);
                    request.executable_fd = invalid;
                }
                if (kind == 2) request.base_env = bad;
                if (kind == 4) request.launch_token = other;
                int result = gkd_app_exec_replace_report(&request, errors[1]), error = errno;
                _exit(result == -1 && ((kind == 1 && error == ENOEXEC) || (kind == 2 && error == EINVAL)) ? 0 : 93);
            }
            if (kind == 3) _exit(0);
            if (kind == 5) CHECK(close(errors[1]) == 0);
            else if (kind == 15 || kind == 16) {
                CHECK(gkd_app_ready_send(ready[1], token) == 0);
                if (kind == 16) { usleep(30000); send_error(errors[1], kind, terminal); }
            } else if (kind != 6 && kind != 12) send_error(errors[1], kind, terminal);
            if (kind != 17) usleep(150000);
            _exit(0);
        }
        int pidfd = syscall(SYS_pidfd_open, child, 0U); CHECK(pidfd >= 3);
        uint64_t now = milliseconds();
        /* Failed init never consumes caller descriptors or changes the object. */
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], getpid(), getuid(), token, now, 1000) == -1);
        CHECK(watch.pidfd == -1 && watch.ready.fd == -1 && fcntl(pidfd, F_GETFD) >= 0);
        CHECK(gkd_app_watch_init(&watch, terminal, errors[0], ready[0], child, getuid(), token, now, 1000) == -1);
        CHECK(fcntl(terminal, F_GETFD) >= 0 && fcntl(errors[0], F_GETFD) >= 0 && fcntl(ready[0], F_GETFD) >= 0);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child, getuid(), token, now, 0) == -1);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child, getuid(), token, UINT64_MAX, 1) == -1);
        CHECK(watch.pidfd == -1 && watch.ready.fd == -1 && fcntl(ready[0], F_GETFD) >= 0);
        int disabled = 0, enabled = 1;
        CHECK(setsockopt(errors[0], SOL_SOCKET, SO_PASSCRED, &disabled, sizeof(disabled)) == 0);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child, getuid(), token, now, 1000) == -1);
        CHECK(setsockopt(errors[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
        CHECK(fcntl(errors[0], F_SETFD, 0) == 0);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child, getuid(), token, now, 1000) == -1);
        CHECK(fcntl(errors[0], F_SETFD, FD_CLOEXEC) == 0);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child,
            kind == 11 ? getuid() + 1 : getuid(), token, now, 1000) == 0);
        CHECK(gkd_app_watch_init(&watch, pidfd, errors[0], ready[0], child, getuid(), token, now, 1000) == -1 && errno == EBUSY);
        if (kind == 6) send_error(errors[1], kind, terminal);
        CHECK(close(errors[1]) == 0 && close(ready[1]) == 0 && close(barrier[0]) == 0);
        CHECK(write(barrier[1], "G", 1) == 1 && close(barrier[1]) == 0);
        enum gkd_app_watch_state result = GKD_WATCH_WAITING;
        for (unsigned int i = 0; i < 2000 && result == GKD_WATCH_WAITING; ++i) {
            uint64_t sample = milliseconds();
            if (kind == 12) sample = now - 1;
            /* Keep time controlled while exercising receipt order. */
            if ((kind == 5 && watch.error_eof) || (kind == 15 && watch.ready.state == GKD_APP_FRAME_SUBMITTED))
                sample = watch.ready.deadline_ms;
            result = gkd_app_watch_poll(&watch, sample);
            if (result == GKD_WATCH_WAITING) usleep(1000);
        }
        enum gkd_app_watch_state expected = GKD_WATCH_BAD_EVENT;
        if (kind == 0) expected = GKD_WATCH_FRAME_SUBMITTED;
        if (kind == 1 || kind == 2 || kind == 16 || kind == 17) expected = GKD_WATCH_EXEC_FAILED;
        if (kind == 3) expected = GKD_WATCH_CHILD_EXITED;
        if (kind == 5 || kind == 15) expected = GKD_WATCH_TIMED_OUT;
        if (kind == 12) expected = GKD_WATCH_CHANNEL_ERROR;
        if (result != expected) {
            fprintf(stderr, "watch case=%s got=%s wanted=%s errno=%d\n", names[kind],
                gkd_app_watch_name(result), gkd_app_watch_name(expected), errno); return 1;
        }
        if (expected == GKD_WATCH_EXEC_FAILED) CHECK(watch.exec_errno == (kind == 2 ? EINVAL : ENOEXEC));
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(gkd_app_watch_poll(&watch, watch.last_ms + 1) == (kind == 0 ? GKD_WATCH_CHILD_EXITED : expected));
        if (kind == 0) CHECK(watch.ready.state == GKD_APP_CHILD_EXITED && watch.error_eof);
        gkd_app_watch_close(&watch); gkd_app_watch_close(&watch);
        CHECK(fcntl(executable, F_GETFD) >= 0 && fcntl(terminal, F_GETFD) >= 0);
        CHECK(descriptors() == initial + 4);
    }
    CHECK(close(executable) == 0 && close(terminal) == 0 && close(output[0]) == 0 &&
          close(output[1]) == 0 && descriptors() == initial);
    puts("GKD_APP_WATCH=PASS lifecycle-cases=18 token-decoder-contracts=PASS exact-exit=PASS fd-leaks=0");
    return 0;
}
