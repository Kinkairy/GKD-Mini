/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui-plane-client.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int gkd_ui_menu_probe_main(int argc, char **argv);
extern int __real_open(const char *path, int flags, ...);
extern int __real_fstat(int fd, struct stat *state);
extern int __real_close(int fd);

#define FRAMEBUFFER_FD 17
static unsigned fb_open_calls, submit_calls, clear_calls, poll_calls;
static int caps_failure, interrupt_poll;
static unsigned expected_transition = 200U;
static uint16_t submitted[GKD_UI_MENU_PIXELS];

int __wrap_open(const char *path, int flags, ...)
{
    va_list ap;
    if (!strcmp(path, "/dev/fb0")) {
        ++fb_open_calls;
        assert(flags == (O_RDWR | O_CLOEXEC | O_NOFOLLOW));
        return FRAMEBUFFER_FD;
    }
    va_start(ap, flags);
    va_end(ap);
    return __real_open(path, flags);
}

int __wrap_fstat(int fd, struct stat *state)
{
    if (fd != FRAMEBUFFER_FD) return __real_fstat(fd, state);
    memset(state, 0, sizeof(*state));
    state->st_mode = S_IFCHR;
    return 0;
}

int __wrap_close(int fd)
{
    return fd == FRAMEBUFFER_FD ? 0 : __real_close(fd);
}

int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list ap;
    void *arg;
    assert(fd == FRAMEBUFFER_FD);
    va_start(ap, command);
    if (command == GKD_UI_MENU_GET_CAPS) {
        struct gkd_ui_menu_caps caps = {GKD_UI_MENU_ABI, GKD_UI_MENU_WIDTH,
            GKD_UI_MENU_HEIGHT, GKD_UI_MENU_RGB565, GKD_UI_MENU_BYTES,
            GKD_UI_MENU_MIN_TTL_MS, GKD_UI_MENU_MAX_TTL_MS, GKD_UI_MENU_MAX_TRANSITION_MS};
        arg = va_arg(ap, void *);
        if (caps_failure) { errno = ENOTTY; va_end(ap); return -1; }
        memcpy(arg, &caps, sizeof(caps));
    } else if (command == GKD_UI_MENU_SUBMIT) {
        const struct gkd_ui_menu_submit *request = va_arg(ap, void *);
        assert(request->pixel_bytes == GKD_UI_MENU_BYTES && request->ttl_ms == 20U &&
               request->sequence == 1U && !request->reserved[0] &&
               !request->reserved[1] && request->transition_ms == expected_transition);
        memcpy(submitted, (const void *)(uintptr_t)request->pixels, sizeof(submitted));
        ++submit_calls;
    } else {
        assert(command == GKD_UI_MENU_CLEAR && va_arg(ap, unsigned long) == 0UL);
        ++clear_calls;
    }
    va_end(ap);
    return 0;
}

int __wrap_poll(void *fds, unsigned long count, int timeout)
{
    (void)fds;
    assert(!count && timeout == 60);
    ++poll_calls;
    if (interrupt_poll) { raise(SIGTERM); errno = EINTR; return -1; }
    return 0;
}

static int create_font(char path[])
{
    unsigned char data[32U + 256U * 16U] = {0};
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    data[0] = 0x72U; data[1] = 0xb5U; data[2] = 0x4aU; data[3] = 0x86U;
    data[8] = 32U; data[16] = 0U; data[17] = 1U; data[20] = 16U;
    data[24] = 16U; data[28] = 8U;
    for (unsigned i = 32U; i < sizeof(data); ++i) data[i] = 0x81U;
    if (write(fd, data, sizeof(data)) != (ssize_t)sizeof(data) || close(fd)) return -1;
    return 0;
}

static void reset(void)
{
    fb_open_calls = submit_calls = clear_calls = poll_calls = 0U;
    caps_failure = interrupt_poll = 0;
    memset(submitted, 0, sizeof(submitted));
}

int main(void)
{
    char font[] = "/tmp/gkd-ui-menu-probe-font-XXXXXX";
    char *bad[] = {"probe", "missing", "USB", "19", "enabled", NULL};
    char *good[] = {"probe", font, "USB", "20", "enabled", NULL};
    char *power[] = {"probe", font, "POWER", "20", "enabled", NULL};
    assert(!create_font(font));
    reset();
    assert(gkd_ui_menu_probe_main(5, bad) == 2 && !fb_open_calls);
    reset(); caps_failure = 1;
    assert(gkd_ui_menu_probe_main(5, good) == 1 && fb_open_calls == 1U &&
           !submit_calls && !clear_calls && !poll_calls);
    reset();
    assert(gkd_ui_menu_probe_main(5, good) == 0 && fb_open_calls == 1U &&
           submit_calls == 1U && !clear_calls && poll_calls == 1U);
    for (unsigned i = 0; i < GKD_UI_MENU_PIXELS; ++i) assert(submitted[i]);
    reset(); interrupt_poll = 1;
    power[4] = "disabled"; expected_transition = 0U;
    assert(gkd_ui_menu_probe_main(5, power) == 0 && submit_calls == 1U &&
           clear_calls == 1U && poll_calls == 1U);
    assert(!unlink(font));
    puts("GKD_UI_MENU_PROBE=PASS render/open/caps/submit/normal/sigterm-clear");
    return 0;
}
