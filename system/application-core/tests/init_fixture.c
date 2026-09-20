/* SPDX-License-Identifier: GPL-2.0 */
/* Native-only workload for unmodified BusyBox init, never installed. */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) _exit(91); } while (0)
static void receipt(const char *name)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "ok\n", 3) == 3 && close(fd) == 0);
}

int main(int argc, char **argv)
{
    unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const char *hex = getenv("GKD_APP_READY_TOKEN");
    int pipes[2], status;
    pid_t helper, orphan;
    (void)argc;
    CHECK(getpid() > 1 && getppid() == 1);
    if (argc > 1 && !strcmp(argv[1], "--prepare-check")) {
        struct sigaction action;
        sigset_t mask;
        CHECK(sigaction(SIGCHLD, NULL, &action) == 0 && action.sa_handler == SIG_DFL);
        CHECK(sigaction(SIGPIPE, NULL, &action) == 0 && action.sa_handler == SIG_DFL);
        CHECK(sigprocmask(SIG_SETMASK, NULL, &mask) == 0 && sigismember(&mask, SIGUSR1) == 0);
        for (int fd = 4; fd <= 100; ++fd) CHECK(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
        receipt("/evidence/C_EXEC_CONTAINED");
    }
    if (hex) {
        CHECK(strlen(hex) == 32);
        for (unsigned i = 0; i < 16; ++i) {
            unsigned value;
            CHECK(isxdigit((unsigned char)hex[i*2]) && isxdigit((unsigned char)hex[i*2+1]));
            CHECK(sscanf(hex + i*2, "%2x", &value) == 1);
            token[i] = (unsigned char)value;
        }
        CHECK(unsetenv("GKD_APP_READY_TOKEN") == 0);
    }
    if (strstr(argv[0], "rcS")) {
        receipt("/evidence/OLD_RCS_EXECUTED");
        return 0;
    }
    CHECK(fcntl(3, F_GETFD) >= 0);
    CHECK(gkd_app_ready_send(3, token) == 0);
    CHECK(close(3) == 0);
    receipt("/evidence/started");
    CHECK(pipe(pipes) == 0);
    helper = fork(); CHECK(helper >= 0);
    if (!helper) {
        close(pipes[0]);
        orphan = fork(); CHECK(orphan >= 0);
        if (!orphan) {
            struct timespec delay = {0, 100000000};
            close(pipes[1]);
            nanosleep(&delay, NULL);
            _exit(0);
        }
        CHECK(write(pipes[1], &orphan, sizeof(orphan)) == sizeof(orphan));
        _exit(0);
    }
    close(pipes[1]);
    CHECK(read(pipes[0], &orphan, sizeof(orphan)) == sizeof(orphan));
    close(pipes[0]);
    CHECK(waitpid(helper, &status, 0) == helper && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* PID 1 must collect the orphan; this process cannot wait for it. */
    for (unsigned i = 0; i < 400; ++i) {
        struct timespec delay = {0, 10000000};
        if (kill(orphan, 0) == -1 && errno == ESRCH) {
            receipt("/evidence/orphan-reaped");
            receipt("/evidence/completed");
            return 0;
        }
        nanosleep(&delay, NULL);
    }
    return 92;
}
