/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-ui-plane-client.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef char submit_size_is_32[(sizeof(struct gkd_ui_plane_submit) == 32U) ? 1 : -1];
typedef char caps_size_is_32[(sizeof(struct gkd_ui_plane_caps) == 32U) ? 1 : -1];
typedef char submit_pointer_at_zero[(offsetof(struct gkd_ui_plane_submit, pixels) == 0U) ? 1 : -1];
typedef char submit_reserved_at_24[(offsetof(struct gkd_ui_plane_submit, reserved) == 24U) ? 1 : -1];
typedef char submit_ioctl_size_is_32[(_IOC_SIZE(GKD_UI_PLANE_SUBMIT) == 32U) ? 1 : -1];
#ifdef __mips__
typedef char mips_has_13_size_bits[(_IOC_SIZEBITS == 13U) ? 1 : -1];
#endif
static unsigned calls;
static int failure, bad_caps, positive;
static struct gkd_ui_plane_submit captured;
int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list ap;
    void *arg;
    assert(fd == 17);
    ++calls;
    if (failure) { errno = failure; return -1; }
    if (positive) return positive;
    va_start(ap, command);
    if (command == GKD_UI_PLANE_CLEAR) {
        assert(va_arg(ap, unsigned long) == 0UL);
    } else {
        arg = va_arg(ap, void *);
        if (command == GKD_UI_PLANE_GET_CAPS) {
            struct gkd_ui_plane_caps value = {GKD_UI_PLANE_ABI,
                GKD_UI_PLANE_X, GKD_UI_PLANE_Y, GKD_UI_PLANE_WIDTH,
                GKD_UI_PLANE_HEIGHT, GKD_UI_PLANE_ARGB8888,
                GKD_UI_PLANE_MAX_TTL_MS, GKD_UI_PLANE_MAX_FADE_MS};
            if (bad_caps) value.width++;
            memcpy(arg, &value, sizeof(value));
        } else {
            assert(command == GKD_UI_PLANE_SUBMIT);
            memcpy(&captured, arg, sizeof(captured));
        }
    }
    va_end(ap);
    return 0;
}
int main(void)
{
    uint32_t pixels[GKD_UI_PLANE_PIXELS] = {0};
    struct gkd_ui_plane_caps caps, before;
    assert(sizeof(struct gkd_ui_plane_submit) == 32U);
    assert(sizeof(struct gkd_ui_plane_caps) == 32U);
    assert(offsetof(struct gkd_ui_plane_submit, pixels) == 0U);
    assert(offsetof(struct gkd_ui_plane_submit, reserved) == 24U);
    assert(_IOC_SIZE(GKD_UI_PLANE_SUBMIT) == 32U);
    memset(&caps, 0xa5, sizeof(caps)); before = caps;
    assert(gkd_ui_plane_capabilities(-1, &caps) == -1 && errno == EBADF && calls == 0U);
    assert(gkd_ui_plane_capabilities(17, NULL) == -1 && errno == EINVAL && calls == 0U);
    bad_caps = 1;
    assert(gkd_ui_plane_capabilities(17, &caps) == -1 && errno == EPROTO &&
           !memcmp(&caps, &before, sizeof(caps)));
    bad_caps = 0;
    assert(!gkd_ui_plane_capabilities(17, &caps) && caps.width == 148U);
    unsigned prior = calls;
    assert(gkd_ui_plane_send(17, NULL, 1000U, 160U, 1U) == -1);
    assert(gkd_ui_plane_send(17, pixels, 0U, 160U, 1U) == -1);
    assert(gkd_ui_plane_send(17, pixels, UINT_MAX, 160U, 1U) == -1);
    assert(gkd_ui_plane_send(17, pixels, 1000U, 1001U, 1U) == -1);
    assert(gkd_ui_plane_send(17, pixels, 1000U, 160U, 0U) == -1);
    assert(calls == prior);
    assert(!gkd_ui_plane_send(17, pixels, 1000U, 160U, 1U));
    assert(captured.pixels == (__u64)(uintptr_t)pixels && captured.pixel_bytes == 11248U);
    assert(captured.ttl_ms == 1000U && captured.fade_ms == 160U && captured.sequence == 1U);
    assert(captured.reserved[0] == 0U && captured.reserved[1] == 0U);
    for (unsigned i = 0; i < 4U; ++i) {
        static const int errors[] = {ENOTTY, EBUSY, EFAULT, EINTR};
        failure = errors[i]; prior = calls;
        assert(gkd_ui_plane_send(17, pixels, 1000U, 160U, 2U) == -1 &&
               errno == failure && calls == prior + 1U);
    }
    failure = 0; positive = 1;
    assert(gkd_ui_plane_send(17, pixels, 1000U, 160U, 2U) == -1 && errno == EPROTO);
    positive = 0;
    assert(!gkd_ui_plane_clear(17));
    assert(gkd_ui_plane_clear(-1) == -1 && errno == EBADF);
    puts("GKD_UI_PLANE_CLIENT=PASS ABI32/validation/one-call/errors/no-fallback");
    return 0;
}
