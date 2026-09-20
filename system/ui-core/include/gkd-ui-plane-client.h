/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_UI_PLANE_CLIENT_H
#define GKD_UI_PLANE_CLIENT_H
#include <stdint.h>
#include "gkd-ui-plane.h"

/* Borrow the caller's fd and pixels. No ownership transfer or implicit retry.
 * A capability mismatch fails; there is no older UI transport fallback. */
int gkd_ui_plane_capabilities(int fd, struct gkd_ui_plane_caps *caps);
int gkd_ui_plane_send(int fd, const uint32_t *pixels, unsigned ttl_ms,
                      unsigned fade_ms, unsigned sequence);
int gkd_ui_plane_clear(int fd);

/* Full-screen menu transport is deliberately separate from the OSD plane.
 * Borrow the caller's fd and RGB565 pixels; there is no input ownership. */
int gkd_ui_menu_capabilities(int fd, struct gkd_ui_menu_caps *caps);
int gkd_ui_menu_send(int fd, const uint16_t *pixels, unsigned ttl_ms,
                     unsigned transition_ms, unsigned sequence);
int gkd_ui_menu_clear(int fd);
int gkd_ui_menu_hide(int fd);
#endif
