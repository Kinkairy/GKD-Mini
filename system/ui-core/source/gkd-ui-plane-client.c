/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-ui-plane-client.h"
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

int gkd_ui_plane_capabilities(int fd, struct gkd_ui_plane_caps *caps)
{
    struct gkd_ui_plane_caps candidate;
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    if (!caps) { errno = EINVAL; return -1; }
    memset(&candidate, 0, sizeof(candidate));
    result = ioctl(fd, GKD_UI_PLANE_GET_CAPS, &candidate);
    if (result < 0) return -1;
    if (result || candidate.abi != GKD_UI_PLANE_ABI ||
        candidate.x != GKD_UI_PLANE_X || candidate.y != GKD_UI_PLANE_Y ||
        candidate.width != GKD_UI_PLANE_WIDTH ||
        candidate.height != GKD_UI_PLANE_HEIGHT ||
        candidate.format != GKD_UI_PLANE_ARGB8888 ||
        candidate.max_ttl_ms != GKD_UI_PLANE_MAX_TTL_MS ||
        candidate.max_fade_ms != GKD_UI_PLANE_MAX_FADE_MS) {
        errno = EPROTO; return -1;
    }
    *caps = candidate;
    return 0;
}

int gkd_ui_plane_send(int fd, const uint32_t *pixels, unsigned ttl_ms,
                      unsigned fade_ms, unsigned sequence)
{
    struct gkd_ui_plane_submit request;
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    if (!pixels || ttl_ms < GKD_UI_PLANE_MIN_TTL_MS ||
        ttl_ms > GKD_UI_PLANE_MAX_TTL_MS ||
        fade_ms > GKD_UI_PLANE_MAX_FADE_MS || !sequence) {
        errno = EINVAL; return -1;
    }
    memset(&request, 0, sizeof(request));
    request.pixels = (__u64)(uintptr_t)pixels;
    request.pixel_bytes = GKD_UI_PLANE_BYTES;
    request.ttl_ms = ttl_ms;
    request.fade_ms = fade_ms;
    request.sequence = sequence;
    result = ioctl(fd, GKD_UI_PLANE_SUBMIT, &request);
    if (result < 0) return -1;
    if (result) { errno = EPROTO; return -1; }
    return 0;
}

int gkd_ui_plane_clear(int fd)
{
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    result = ioctl(fd, GKD_UI_PLANE_CLEAR, 0UL);
    if (result < 0) return -1;
    if (result) { errno = EPROTO; return -1; }
    return 0;
}

int gkd_ui_menu_capabilities(int fd, struct gkd_ui_menu_caps *caps)
{
    struct gkd_ui_menu_caps candidate;
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    if (!caps) { errno = EINVAL; return -1; }
    memset(&candidate, 0, sizeof(candidate));
    result = ioctl(fd, GKD_UI_MENU_GET_CAPS, &candidate);
    if (result < 0) return -1;
    if (result || candidate.abi != GKD_UI_MENU_ABI ||
        candidate.width != GKD_UI_MENU_WIDTH ||
        candidate.height != GKD_UI_MENU_HEIGHT ||
        candidate.format != GKD_UI_MENU_RGB565 ||
        candidate.pixel_bytes != GKD_UI_MENU_BYTES ||
        candidate.min_ttl_ms != GKD_UI_MENU_MIN_TTL_MS ||
        candidate.max_ttl_ms != GKD_UI_MENU_MAX_TTL_MS ||
        candidate.max_transition_ms != GKD_UI_MENU_MAX_TRANSITION_MS) {
        errno = EPROTO; return -1;
    }
    *caps = candidate;
    return 0;
}

int gkd_ui_menu_send(int fd, const uint16_t *pixels, unsigned ttl_ms,
                     unsigned transition_ms, unsigned sequence)
{
    struct gkd_ui_menu_submit request;
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    if (!pixels || ttl_ms < GKD_UI_MENU_MIN_TTL_MS ||
        ttl_ms > GKD_UI_MENU_MAX_TTL_MS ||
        transition_ms > GKD_UI_MENU_MAX_TRANSITION_MS || !sequence) {
        errno = EINVAL; return -1;
    }
    memset(&request, 0, sizeof(request));
    request.pixels = (__u64)(uintptr_t)pixels;
    request.pixel_bytes = GKD_UI_MENU_BYTES;
    request.ttl_ms = ttl_ms;
    request.transition_ms = transition_ms;
    request.sequence = sequence;
    result = ioctl(fd, GKD_UI_MENU_SUBMIT, &request);
    if (result < 0) return -1;
    if (result) { errno = EPROTO; return -1; }
    return 0;
}

int gkd_ui_menu_clear(int fd)
{
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    result = ioctl(fd, GKD_UI_MENU_CLEAR, 0UL);
    if (result < 0) return -1;
    if (result) { errno = EPROTO; return -1; }
    return 0;
}

int gkd_ui_menu_hide(int fd)
{
    int result;
    if (fd < 0) { errno = EBADF; return -1; }
    result = ioctl(fd, GKD_UI_MENU_HIDE, 0UL);
    if (result < 0) return -1;
    if (result) { errno = EPROTO; return -1; }
    return 0;
}
