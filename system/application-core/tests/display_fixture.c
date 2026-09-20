/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-display.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line=%d expr=%s errno=%d", __LINE__, #x, errno); exit(1); } } while (0)
enum fault {NONE, OPEN, GETSTATE, GETMODE, KDGET, KDSET, ACTIVATE, STUCK, CLOCK, PAUSE, DRIFT};
static enum fault failure;
static int fd_vt[256], open_count, active_vt, modes[3], displays[3], writes, polls, pending, reads;
static uint64_t time_ms;
static int target_fault, switch_locked, unlocks, activations, unlock_failure;
int __real_open(const char *, int, ...);
int __real_close(int);
int __wrap_open(const char *path, int flags, ...)
{
    CHECK(flags & O_CLOEXEC); CHECK(flags & O_NOCTTY);
    if (failure == OPEN) { errno = ENOENT; return -1; }
    CHECK(!strncmp(path, "/dev/tty", 8));
    int terminal = atoi(path + 8); CHECK(terminal >= 0 && terminal <= 2);
    int fd = __real_open("/dev/null", O_RDWR | O_CLOEXEC); CHECK(fd >= 0 && fd < 256);
    fd_vt[fd] = terminal; ++open_count; return fd;
}
int __wrap_close(int fd)
{
    CHECK(fd >= 0 && fd < 256 && fd_vt[fd] >= 0);
    fd_vt[fd] = -1; --open_count; return __real_close(fd);
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *time)
{
    CHECK(clock == CLOCK_MONOTONIC);
    if (failure == CLOCK) { errno = EIO; return -1; }
    ++time_ms; time->tv_sec = time_ms / 1000; time->tv_nsec = time_ms % 1000 * 1000000;
    return 0;
}
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    CHECK(!fds && !count && timeout > 0 && timeout <= 10);
    if (failure == PAUSE) { errno = EINTR; return -1; }
    time_ms += timeout; ++polls;
    if (pending && polls >= pending && failure != STUCK) active_vt = 2;
    return 0;
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    CHECK(fd >= 0 && fd < 256 && fd_vt[fd] >= 0);
    int terminal = fd_vt[fd] ? fd_vt[fd] : active_vt;
    va_list args; va_start(args, request);
    if (request == VT_GETSTATE) {
        struct vt_stat *state = va_arg(args, struct vt_stat *);
        va_end(args); ++reads;
        if (failure == GETSTATE) { errno = EIO; return -1; }
        if (failure == DRIFT && reads == 2) active_vt = 2;
        memset(state, 0, sizeof(*state)); state->v_active = active_vt; return 0;
    }
    if (request == VT_GETMODE) {
        struct vt_mode *mode = va_arg(args, struct vt_mode *); va_end(args);
        if (failure == GETMODE) { errno = EIO; return -1; }
        memset(mode, 0, sizeof(*mode)); mode->mode = modes[terminal]; return 0;
    }
    if (request == KDGETMODE) {
        int *display = va_arg(args, int *); va_end(args);
        if (failure == KDGET || (target_fault == 1 && terminal == 2)) { errno = EIO; return -1; }
        *display = displays[terminal]; return 0;
    }
    unsigned long value = va_arg(args, unsigned long); va_end(args);
    if (request == KDSETMODE) {
        CHECK(value == KD_TEXT);
        if (failure == KDSET || (target_fault == 2 && terminal == 2)) { errno = EIO; return -1; }
        displays[terminal] = value; ++writes; return 0;
    }
    if (request == VT_UNLOCKSWITCH) {
        CHECK(value == 0 && fd_vt[fd] == 0);
        CHECK(modes[1] == VT_AUTO && modes[2] == VT_AUTO);
        CHECK(displays[active_vt] == KD_TEXT && displays[2] == KD_TEXT);
        ++unlocks;
        if (unlock_failure) { errno = EPERM; return -1; }
        switch_locked = 0; return 0;
    }
    if (request == VT_ACTIVATE) {
        CHECK(value == 2); ++activations;
        if (failure == ACTIVATE) { errno = EIO; return -1; }
        /* Exact pinned-kernel failure: silently refuse auto+graphics switch. */
        if (switch_locked || (modes[active_vt] == VT_AUTO && displays[active_vt] == KD_GRAPHICS)) return 0;
        if (failure == STUCK || failure == PAUSE) return 0;
        if (!pending) active_vt = 2;
        return 0;
    }
    /* Production must never invoke VT_WAITACTIVE or mutate VT ownership mode. */
    CHECK(0); return -1;
}
static void reset(void)
{
    CHECK(open_count == 0); memset(fd_vt, 0xff, sizeof(fd_vt));
    failure = NONE; active_vt = 1; modes[1] = modes[2] = VT_AUTO;
    displays[1] = KD_GRAPHICS; displays[2] = KD_TEXT;
    writes = polls = pending = reads = target_fault = unlocks = activations = unlock_failure = 0;
    switch_locked = 1; time_ms = 1000;
}
int main(void)
{
    reset();
    int fd = __wrap_open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    CHECK(__wrap_ioctl(fd, VT_ACTIVATE, (unsigned long)2) == 0);
    CHECK(active_vt == 1);
    displays[1] = KD_TEXT;
    CHECK(__wrap_ioctl(fd, VT_ACTIVATE, (unsigned long)2) == 0 && active_vt == 1);
    displays[1] = KD_GRAPHICS; __wrap_close(fd);
    CHECK(gkd_app_display_prepare(1000) == 0 && active_vt == 2 && writes == 1 && !open_count && unlocks == 1 && !switch_locked);
    puts("CASE=confirmed-warm-graphics-and-global-lock-regression PASS");
    reset(); displays[1] = KD_TEXT; switch_locked = 0;
    CHECK(gkd_app_display_prepare(1000) == 0 && active_vt == 2 && !writes && !open_count);
    puts("CASE=cold-text-no-unnecessary-mode-write PASS");
    reset(); active_vt = 2; displays[2] = KD_GRAPHICS;
    CHECK(gkd_app_display_prepare(1000) == 0 && active_vt == 2 && writes == 1 && !open_count);
    puts("CASE=already-target-graphics PASS");
    for (int vt = 1; vt <= 2; ++vt) {
        reset(); modes[vt] = VT_PROCESS;
        CHECK(gkd_app_display_prepare(1000) == -1 && errno == EBUSY && !writes && !open_count && !unlocks && switch_locked);
    }
    puts("CASE=active-and-target-process-owner-refusal PASS");
    reset(); pending = 3;
    CHECK(gkd_app_display_prepare(1000) == 0 && polls == 3 && active_vt == 2 && !open_count);
    puts("CASE=asynchronous-switch-poll PASS");
    for (enum fault fault = OPEN; fault <= DRIFT; ++fault) {
        reset(); failure = fault;
        CHECK(gkd_app_display_prepare(40) == -1 && !open_count);
        if (fault == STUCK) CHECK(errno == ETIMEDOUT && time_ms < 1060);
        if (fault == DRIFT) CHECK(errno == EBUSY && !writes);
    }
    puts("CASE=11-open-query-set-activate-clock-poll-timeout-drift-faults PASS");
    for (int fault = 1; fault <= 2; ++fault) {
        reset(); target_fault = fault; displays[2] = KD_GRAPHICS;
        CHECK(gkd_app_display_prepare(1000) == -1 && !open_count);
        CHECK(writes == 1 && displays[1] == KD_TEXT && displays[2] == KD_GRAPHICS && active_vt == 1 && !unlocks && switch_locked);
    }
    puts("CASE=target-get-and-set-fail-after-active-normalization PASS");
    reset();
    CHECK(gkd_app_display_prepare(0) == -1 && gkd_app_display_prepare(1001) == -1 && !open_count);
    puts("CASE=invalid-deadline-refusal PASS");
    reset(); displays[1] = KD_TEXT;
    CHECK(gkd_app_display_prepare(1000) == 0 && active_vt == 2 && !writes && unlocks == 1 && !switch_locked && !open_count);
    puts("CASE=locked-text-independent-of-graphics PASS");
    reset(); switch_locked = 0;
    CHECK(gkd_app_display_prepare(1000) == 0 && active_vt == 2 && writes == 1 && unlocks == 1 && !open_count);
    puts("CASE=unlocked-graphics-single-acquisition-path PASS");
    reset(); unlock_failure = 1;
    CHECK(gkd_app_display_prepare(1000) == -1 && errno == EPERM && active_vt == 1 && unlocks == 1 && switch_locked && !activations && !open_count);
    puts("CASE=unlock-refusal-blocks-activation PASS");
    puts("GKD_APP_DISPLAY=PASS cases=23 sanitizer=ASan+UBSan");
    return 0;
}
