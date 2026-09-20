/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef GKD_UI_PLANE_H
#define GKD_UI_PLANE_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Private GKD Mini framebuffer ABI. The MIPS ioctl size field is 13 bits:
 * keep the request small and capture its pointed-to pixels exactly once. */
#define GKD_UI_PLANE_ABI 1U
#define GKD_UI_PLANE_X 8U
#define GKD_UI_PLANE_Y 206U
#define GKD_UI_PLANE_WIDTH 148U
#define GKD_UI_PLANE_HEIGHT 19U
#define GKD_UI_PLANE_PIXELS (GKD_UI_PLANE_WIDTH * GKD_UI_PLANE_HEIGHT)
#define GKD_UI_PLANE_BYTES (GKD_UI_PLANE_PIXELS * 4U)
#define GKD_UI_PLANE_ARGB8888 0x34325241U
#define GKD_UI_PLANE_MIN_TTL_MS 20U
#define GKD_UI_PLANE_MAX_TTL_MS 10000U
#define GKD_UI_PLANE_MAX_FADE_MS 1000U

struct gkd_ui_plane_caps {
    __u32 abi;
    __u32 x;
    __u32 y;
    __u32 width;
    __u32 height;
    __u32 format;
    __u32 max_ttl_ms;
    __u32 max_fade_ms;
};

struct gkd_ui_plane_submit {
    __aligned_u64 pixels;
    __u32 pixel_bytes;
    __u32 ttl_ms;       /* Total remaining lifetime, including fade-out. */
    __u32 fade_ms;
    __u32 sequence;     /* Nonzero, strictly increasing within a live lease. */
    __u32 reserved[2];  /* Must be zero. */
};

/* All commands return zero on success. Unknown commands return ENOTTY.
 * SUBMIT and CLEAR require CAP_SYS_ADMIN. The publisher is the kernel TGID,
 * not an arbitrary user-supplied id or an open-file-exclusive owner.
 * A competing process may publish only after expiry. A live owner's SUBMIT
 * renews expiry without restarting fade-in. CLEAR is owner-only/idempotent
 * when no live lease exists. Closing an fd does not revoke a process lease. */
#define GKD_UI_PLANE_GET_CAPS _IOR('G', 0x70, struct gkd_ui_plane_caps)
#define GKD_UI_PLANE_SUBMIT _IOW('G', 0x71, struct gkd_ui_plane_submit)
#define GKD_UI_PLANE_CLEAR _IO('G', 0x72)

/* Separate opaque full-screen menu plane. This does not change OSD ABI 1.
 * RGB565 words use the target's native little-endian byte order. The menu
 * replaces only compositor output; application framebuffer pages stay intact.
 * OSD is composited above it. Lease/security rules match the OSD commands;
 * ABI 2 adds bounded vertical motion, with no input/application ownership.
 * HIDE slides up from current position without extending TTL; CLEAR remains
 * immediate emergency cleanup. Same-owner SUBMIT may reverse a live HIDE. */
#define GKD_UI_MENU_ABI 2U
#define GKD_UI_MENU_WIDTH 320U
#define GKD_UI_MENU_HEIGHT 240U
#define GKD_UI_MENU_PIXELS (GKD_UI_MENU_WIDTH * GKD_UI_MENU_HEIGHT)
#define GKD_UI_MENU_BYTES (GKD_UI_MENU_PIXELS * 2U)
#define GKD_UI_MENU_RGB565 0x36314752U
#define GKD_UI_MENU_MIN_TTL_MS 20U
#define GKD_UI_MENU_MAX_TTL_MS 10000U
#define GKD_UI_MENU_MAX_TRANSITION_MS 1000U

struct gkd_ui_menu_caps {
    __u32 abi;
    __u32 width;
    __u32 height;
    __u32 format;
    __u32 pixel_bytes;
    __u32 min_ttl_ms;
    __u32 max_ttl_ms;
    __u32 max_transition_ms;
};

struct gkd_ui_menu_submit {
    __aligned_u64 pixels;
    __u32 pixel_bytes;
    __u32 ttl_ms;
    __u32 sequence;
    __u32 transition_ms; /* Zero disables motion; TTL includes slide-out. */
    __u32 reserved[2];
};

#define GKD_UI_MENU_GET_CAPS _IOR('G', 0x73, struct gkd_ui_menu_caps)
#define GKD_UI_MENU_SUBMIT _IOW('G', 0x74, struct gkd_ui_menu_submit)
#define GKD_UI_MENU_CLEAR _IO('G', 0x75)
#define GKD_UI_MENU_HIDE _IO('G', 0x76)

/* A-profile application handoff underlay lease. SUBMIT captures the current
 * application scanout once for a new TGID lease. A live owner renews only the
 * expiry and must use a strictly increasing nonzero sequence, so the original
 * capture stays stable. A competing TGID is rejected until expiry or process
 * death. CLEAR is owner-only while live and is otherwise idempotent.
 *
 * This lease never blocks FBIOPAN_DISPLAY, never invokes the legacy USB menu,
 * and uses the existing compositor snapshot instead of another framebuffer. */
#define GKD_UI_FREEZE_MIN_TTL_MS 20U
#define GKD_UI_FREEZE_MAX_TTL_MS 10000U

struct gkd_ui_freeze_submit {
	__u32 ttl_ms;
	__u32 sequence;
	__u32 reserved[2];
};

#define GKD_UI_FREEZE_SUBMIT _IOW('G', 0x77, struct gkd_ui_freeze_submit)
#define GKD_UI_FREEZE_CLEAR _IO('G', 0x78)

#endif
