/* SPDX-License-Identifier: GPL-2.0 */
/* Native linker-only delayed-entropy/release fixture; never in the capsule. */
#define _GNU_SOURCE
#include "gkd-app-release.h"
#include "gkd-app-exec.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long offset_ms;
static unsigned token_calls, release_calls;
static unsigned output_polls;
static int entropy_pending;
static int selected(const char *name)
{
    const char *value = getenv("GKD_HOST_TIME_FIXTURE");
    return value && !strcmp(value, name);
}
int __real_clock_gettime(clockid_t clock, struct timespec *value);
int __wrap_clock_gettime(clockid_t clock, struct timespec *value)
{
    int result = __real_clock_gettime(clock, value);
    if (!result && clock == CLOCK_MONOTONIC) {
        value->tv_sec += offset_ms / 1000;
        value->tv_nsec += offset_ms % 1000 * 1000000L;
        if (value->tv_nsec >= 1000000000L) { ++value->tv_sec; value->tv_nsec -= 1000000000L; }
    }
    return result;
}
int __real_poll(struct pollfd *fds, nfds_t n, int timeout);
int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout)
{
    if (!fds && !n && selected("output-flood-cancel") && release_calls && ++output_polls == 20) {
        raise(SIGTERM);
        return 0;
    }
    if (!fds && !n && entropy_pending) {
        offset_ms += selected("entropy-timeout") ? 6000 : 4900;
        entropy_pending = 0;
        return 0;
    }
    return __real_poll(fds, n, timeout);
}
int __real_gkd_app_launch_token(unsigned char token[16]);
int __wrap_gkd_app_launch_token(unsigned char token[16])
{
    ++token_calls;
    if (selected("entropy-timeout") || (selected("late-entropy") && token_calls == 1)) {
        memset(token, 0, 16); entropy_pending = 1; errno = EAGAIN; return -1;
    }
    return __real_gkd_app_launch_token(token);
}
int __real_gkd_app_release_send(struct gkd_app_release_tx *, struct gkd_app_watch *, int,
                               int, pid_t, int, uint64_t);
int __wrap_gkd_app_release_send(struct gkd_app_release_tx *tx, struct gkd_app_watch *watch,
                               int fd, int pidfd, pid_t pid, int image, uint64_t now)
{
    if (selected("cancel-before-release")) {
        raise(SIGTERM);
        errno = EAGAIN;
        return -1;
    }
    if (selected("late-entropy") && !release_calls++) {
        offset_ms += 300;
        errno = EAGAIN;
        return -1;
    }
    int result = __real_gkd_app_release_send(tx, watch, fd, pidfd, pid, image, now);
    if (!result && selected("output-flood-cancel")) release_calls = 1;
    return result;
}

int __real_gkd_app_exec_replace_report(const struct gkd_app_exec_request *, int);
int __wrap_gkd_app_exec_replace_report(const struct gkd_app_exec_request *r, int error_fd)
{
    if (selected("output-flood-timeout") || selected("output-flood-cancel")) {
        char block[4096]; memset(block, 'x', sizeof(block));
        /* An intentionally noisy child never reaches exec/readiness. The real
         * controller must still enforce deadline/cancellation and reap it. */
        for (;;) {
            ssize_t n = write(r->output_fd, block, sizeof(block));
            if (n < 0 && errno != EAGAIN && errno != EINTR) _exit(125);
            (void)__real_poll(NULL, 0, 1);
        }
    }
    return __real_gkd_app_exec_replace_report(r, error_fd);
}
