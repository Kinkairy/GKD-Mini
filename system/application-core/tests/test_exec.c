/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-exec.h"
#include "gkd-app-ready.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "exec line=%d errno=%d\n", __LINE__, errno); exit(1); } } while (0)
static const unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
int main(void)
{
    char *argv[] = {"exec-fixture", NULL};
    char *environment[] = {"HOME=/fixture-home", "PATH=/bin", NULL};
    unsigned char zero[16] = {0};
    struct rlimit limit;
    CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_max > 4096);
    if (limit.rlim_cur <= 4096) { limit.rlim_cur = 4097; CHECK(setrlimit(RLIMIT_NOFILE, &limit) == 0); }
    CHECK(link("/test/exec-fixture", "/test/pinned-fixture") == 0);
    int executable = open("/test/pinned-fixture", O_RDONLY | O_CLOEXEC);
    CHECK(executable >= 3 && unlink("/test/pinned-fixture") == 0);
    int terminal = open("/dev/null", O_RDWR | O_CLOEXEC);
    int noise = open("/test/parent-owned", O_CREAT | O_EXCL | O_RDWR, 0600);
    int high = fcntl(noise, F_DUPFD, 4096);
    CHECK(terminal >= 3 && noise >= 3 && high >= 4096);
    for (unsigned int kind = 0; kind < 23; ++kind) {
        int pair[2], output[2], status;
        struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
        CHECK(gkd_app_ready_channel(pair) == 0);
        CHECK(pipe2(output, O_NONBLOCK | O_CLOEXEC) == 0);
        struct gkd_app_exec_request request = {
            getpid(), executable, terminal, output[1], pair[1], "/test/libgkd-sm-present.so",
            token, argv, environment
        };
        CHECK(gkd_app_exec_replace(&request) == -1 && errno == EPERM);
        pid_t child = fork(); CHECK(child >= 0);
        if (!child) {
            char *bad_env[] = {"LD_PRELOAD=/wrong", NULL};
            char *duplicate[] = {"PATH=/bin", "PATH=/other", NULL};
            char *reserved[] = {"GKD_APP_READY_FD=99", NULL};
            char *audit[] = {"LD_AUDIT=/wrong", NULL};
            char *malformed[] = {"1INVALID=value", NULL};
            char *missing_equal[] = {"HOME", NULL};
            char *many_args[65];
            for (unsigned int i = 0; i < 64; ++i) many_args[i] = "argument";
            many_args[64] = NULL;
            if (kind == 1) request.base_env = bad_env;
            if (kind == 2) request.base_env = duplicate;
            if (kind == 3) request.launch_token = zero;
            if (kind == 4) request.terminal_fd = executable;
            if (kind == 5) request.executable_fd = terminal;
            if (kind == 6) {
                int invalid = memfd_create("invalid-elf", MFD_CLOEXEC);
                CHECK(invalid >= 3 && write(invalid, "\177ELF", 4) == 4);
                request.executable_fd = invalid;
            }
            if (kind == 7) request.preload_path = "relative.so";
            if (kind == 8) request.base_env = reserved;
            if (kind == 9) request.base_env = audit;
            if (kind == 10) request.base_env = malformed;
            if (kind == 11) request.base_env = missing_equal;
            if (kind == 12) request.argv = many_args;
            if (kind == 13) request.argv = NULL;
            if (kind == 14) request.output_fd = -1;
            if (kind == 15) request.output_fd = pair[1];
            if (kind == 16) request.output_fd = terminal;
            if (kind == 17) request.output_fd = output[0];
            if (kind == 18) request.output_fd = noise;
            if (kind == 19) CHECK(fcntl(output[1], F_SETFD, 0) == 0);
            if (kind == 20) CHECK(fcntl(output[1], F_SETFL, O_WRONLY) == 0);
            if (kind == 21) CHECK(close(output[1]) == 0);
            if (kind == 22) request.output_fd = executable;
            int result = gkd_app_exec_replace(&request), error = errno;
            if (!kind) _exit(92); /* successful exec must never return */
            if (kind == 6) _exit(result == -1 && error == ENOEXEC ? 0 : 93);
            /* Invalid inputs are rejected before descriptor mutation. */
            _exit(result == -1 && error == EINVAL && fcntl(high, F_GETFD) >= 0 &&
                  fcntl(noise, F_GETFD) >= 0 ? 0 : 94);
        }
        CHECK(gkd_app_ready_init(&gate, pair[0], child, getuid(), token, 100, 100) == 0);
        CHECK(close(pair[1]) == 0 && close(output[1]) == 0);
        CHECK(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "exec case=%u status=%d\n", kind, status); return 1;
        }
        /* Native protocol test only: real launcher must observe actual liveness. */
        CHECK(gkd_app_ready_poll(&gate, 101, 1) ==
              (kind == 0 ? GKD_APP_FRAME_SUBMITTED : GKD_APP_WAITING));
        gkd_app_ready_close(&gate);
        if (!kind) {
            char text[15] = {0};
            CHECK(read(output[0], text, 14) == 14 && !strcmp(text, "stdout\nstderr\n"));
            CHECK(read(output[0], text, 1) == 0);
        }
        CHECK(close(output[0]) == 0);
        CHECK(fcntl(executable, F_GETFD) >= 0 && fcntl(terminal, F_GETFD) >= 0);
        CHECK(fcntl(noise, F_GETFD) >= 0 && fcntl(high, F_GETFD) >= 0);
        CHECK(write(noise, "ok", 2) == 2);
    }
    CHECK(close(executable) == 0 && close(terminal) == 0 && close(noise) == 0 && close(high) == 0);
    puts("GKD_APP_EXEC=PASS cases=23 pinned-ELF=PASS fd4096-contained=PASS parent-preserved=PASS output-routing=PASS");
    return 0;
}
