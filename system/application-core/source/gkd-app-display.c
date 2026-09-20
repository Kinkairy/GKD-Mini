/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-display.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int tick(uint64_t *ms)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    *ms = (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
    return 0;
}
static int text_mode(int fd)
{
    struct vt_mode mode;
    int display;
    if (ioctl(fd, VT_GETMODE, &mode) || ioctl(fd, KDGETMODE, &display)) return -1;
    if (mode.mode != VT_AUTO) { errno = EBUSY; return -1; }
    if (display == KD_TEXT) return 0;
    if (display != KD_GRAPHICS) { errno = EINVAL; return -1; }
    return ioctl(fd, KDSETMODE, (unsigned long)KD_TEXT);
}
int gkd_app_display_prepare(unsigned timeout_ms)
{
    int control = -1, active = -1, target = -1, result = -1, saved;
    struct vt_stat original, current;
    uint64_t now, deadline;
    char path[32];
    if (!timeout_ms || timeout_ms > 1000) { errno = EINVAL; return -1; }
    if (tick(&now)) return -1;
    deadline = now + timeout_ms;
    control = open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (control < 0 || ioctl(control, VT_GETSTATE, &original)) goto done;
    if (!original.v_active || original.v_active > 63) { errno = EINVAL; goto done; }
    snprintf(path, sizeof(path), "/dev/tty%u", original.v_active);
    active = open(path, O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (active < 0) goto done;
    target = open("/dev/tty2", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (target < 0) goto done;
    /* Do not mutate either terminal until both ownership modes are accepted. */
    struct vt_mode active_mode, target_mode;
    if (ioctl(active, VT_GETMODE, &active_mode) || ioctl(target, VT_GETMODE, &target_mode)) goto done;
    if (active_mode.mode != VT_AUTO || target_mode.mode != VT_AUTO) { errno = EBUSY; goto done; }
    if (ioctl(control, VT_GETSTATE, &current)) goto done;
    if (current.v_active != original.v_active) { errno = EBUSY; goto done; }
    if (text_mode(active) || text_mode(target)) goto done;
    /* A terminated application may leave the independent global switch lock.
     * Sole startup ownership is required; never unlock a live process owner. */
    if (ioctl(control, VT_UNLOCKSWITCH, (unsigned long)0)) goto done;
    if (ioctl(control, VT_ACTIVATE, (unsigned long)2)) goto done;
    for (;;) {
        if (tick(&now)) goto done;
        if (now >= deadline) { errno = ETIMEDOUT; goto done; }
        if (ioctl(control, VT_GETSTATE, &current)) goto done;
        if (current.v_active == 2) { result = 0; break; }
        unsigned pause = deadline - now < 10 ? (unsigned)(deadline - now) : 10;
        if (poll(NULL, 0, (int)pause) < 0) goto done;
    }
done:
    saved = errno;
    if (target >= 0) close(target);
    if (active >= 0) close(active);
    if (control >= 0) close(control);
    errno = result ? saved : 0;
    return result;
}
