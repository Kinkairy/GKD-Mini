/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-ui-plane-client.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef char menu_caps_size_is_32[(sizeof(struct gkd_ui_menu_caps) == 32U) ? 1 : -1];
typedef char menu_submit_size_is_32[(sizeof(struct gkd_ui_menu_submit) == 32U) ? 1 : -1];
typedef char menu_submit_pointer_at_zero[(offsetof(struct gkd_ui_menu_submit, pixels) == 0U) ? 1 : -1];
typedef char menu_submit_reserved_at_24[(offsetof(struct gkd_ui_menu_submit, reserved) == 24U) ? 1 : -1];
typedef char menu_submit_ioctl_size_is_32[(_IOC_SIZE(GKD_UI_MENU_SUBMIT) == 32U) ? 1 : -1];
#ifdef __mips__
typedef char mips_has_13_size_bits[(_IOC_SIZEBITS == 13U) ? 1 : -1];
#endif

static unsigned calls;
static int failure, bad_caps, positive;
static struct gkd_ui_menu_submit captured;

int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list ap;
    void *arg;
    assert(fd == 17);
    ++calls;
    if (failure) { errno = failure; return -1; }
    if (positive) return positive;
    va_start(ap, command);
    if ((command == GKD_UI_MENU_CLEAR || command == GKD_UI_MENU_HIDE)) {
        assert(va_arg(ap, unsigned long) == 0UL);
    } else {
        arg = va_arg(ap, void *);
        if (command == GKD_UI_MENU_GET_CAPS) {
            struct gkd_ui_menu_caps value = {GKD_UI_MENU_ABI,
                GKD_UI_MENU_WIDTH, GKD_UI_MENU_HEIGHT, GKD_UI_MENU_RGB565,
                GKD_UI_MENU_BYTES, GKD_UI_MENU_MIN_TTL_MS,
                GKD_UI_MENU_MAX_TTL_MS, GKD_UI_MENU_MAX_TRANSITION_MS};
            switch (bad_caps) {
            case 1: value.abi++; break;
            case 2: value.width++; break;
            case 3: value.height++; break;
            case 4: value.format++; break;
            case 5: value.pixel_bytes++; break;
            case 6: value.min_ttl_ms++; break;
            case 7: value.max_ttl_ms--; break;
            case 8: value.max_transition_ms++; break;
            default: break;
            }
            memcpy(arg, &value, sizeof(value));
        } else {
            assert(command == GKD_UI_MENU_SUBMIT);
            memcpy(&captured, arg, sizeof(captured));
        }
    }
    va_end(ap);
    return 0;
}

int main(void)
{
    uint16_t pixels[GKD_UI_MENU_PIXELS] = {0};
    struct gkd_ui_menu_caps caps, before;
    unsigned prior;
    assert(sizeof(struct gkd_ui_menu_caps) == 32U);
    assert(sizeof(struct gkd_ui_menu_submit) == 32U);
    assert(offsetof(struct gkd_ui_menu_submit, pixels) == 0U);
    assert(offsetof(struct gkd_ui_menu_submit, reserved) == 24U);
    assert(_IOC_SIZE(GKD_UI_MENU_SUBMIT) == 32U);
    memset(&caps, 0xa5, sizeof(caps)); before = caps;
    assert(gkd_ui_menu_capabilities(-1, &caps) == -1 && errno == EBADF && calls == 0U);
    assert(gkd_ui_menu_capabilities(17, NULL) == -1 && errno == EINVAL && calls == 0U);
    for (bad_caps = 1; bad_caps <= 8; ++bad_caps) {
        assert(gkd_ui_menu_capabilities(17, &caps) == -1 && errno == EPROTO &&
               !memcmp(&caps, &before, sizeof(caps)));
    }
    bad_caps = 0;
    failure = ENOTTY;
    assert(gkd_ui_menu_capabilities(17, &caps) == -1 && errno == ENOTTY &&
           !memcmp(&caps, &before, sizeof(caps)));
    failure = 0;
    assert(!gkd_ui_menu_capabilities(17, &caps) && caps.width == GKD_UI_MENU_WIDTH);
    prior = calls;
    assert(gkd_ui_menu_send(17, NULL, 1000U, 200U, 1U) == -1 && errno == EINVAL);
    assert(gkd_ui_menu_send(17, pixels, GKD_UI_MENU_MIN_TTL_MS - 1U, 200U, 1U) == -1 && errno == EINVAL);
    assert(gkd_ui_menu_send(17, pixels, GKD_UI_MENU_MAX_TTL_MS + 1U, 200U, 1U) == -1 && errno == EINVAL);
    assert(gkd_ui_menu_send(17, pixels, 1000U, 200U, 0U) == -1 && errno == EINVAL);
    assert(gkd_ui_menu_send(17, pixels, 1000U, 1001U, 1U) == -1 && errno == EINVAL);
    assert(calls == prior);
    assert(!gkd_ui_menu_send(17, pixels, 1000U, 200U, 1U));
    assert(captured.pixels == (__u64)(uintptr_t)pixels &&
           captured.pixel_bytes == GKD_UI_MENU_BYTES && captured.ttl_ms == 1000U &&
           captured.sequence == 1U && !captured.reserved[0] && !captured.reserved[1] &&
           captured.transition_ms == 200U);
    for (unsigned i = 0; i < 4U; ++i) {
        static const int errors[] = {ENOTTY, EBUSY, EFAULT, EINTR};
        failure = errors[i]; prior = calls;
        assert(gkd_ui_menu_send(17, pixels, 1000U, 200U, 2U) == -1 && errno == failure &&
               calls == prior + 1U);
    }
    failure = 0; positive = 1;
    assert(gkd_ui_menu_send(17, pixels, 1000U, 200U, 2U) == -1 && errno == EPROTO);
    positive = 0;
    assert(!gkd_ui_menu_hide(17));
    assert(gkd_ui_menu_hide(-1) == -1 && errno == EBADF);
    assert(!gkd_ui_menu_clear(17));
    assert(gkd_ui_menu_clear(-1) == -1 && errno == EBADF);
    puts("GKD_UI_MENU_CLIENT=PASS ABI32/caps/reserved/validation/one-call/errors/no-fallback");
    return 0;
}
