/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef GKD_MENU_VT_H
#define GKD_MENU_VT_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define GKD_MENU_VT_VERSION 2U
#define GKD_MENU_VT_KEYS 4U
#define GKD_INPUT_ROUTE_MAPS 32U
struct gkd_input_route_map { __u16 source, target; };
struct gkd_menu_vt_config {
    __u32 version, count, hold_ms, reserved;
    __u16 keys[GKD_MENU_VT_KEYS];
    __u16 trigger, map_count;
    struct gkd_input_route_map maps[GKD_INPUT_ROUTE_MAPS];
};
struct gkd_menu_vt_receipt { __u32 version; __s32 error; };
#define GKD_MENU_VT_CONFIG _IOW('G', 0xe0, struct gkd_menu_vt_config)
#define GKD_MENU_VT_PULSE _IO('G', 0xe1)
#define GKD_MENU_VT_CANCEL _IO('G', 0xe2)
/* Independent system-prefix owner; game routing remains a separate lease. */
#define GKD_SYSTEM_HOTKEY_VERSION 1U
struct gkd_system_hotkey_config {
    __u32 version, reserved;
    __u16 prefix, button;
};
#define GKD_SYSTEM_HOTKEY_CONFIG _IOW('G', 0xe3, struct gkd_system_hotkey_config)
/* Atomically replace only the game maps on the existing lease. Busy holds
 * and MENU receipts must drain first; close still restores native routing. */
#define GKD_INPUT_ROUTE_UPDATE _IOW('G', 0xe4, struct gkd_menu_vt_config)
/* Atomic game-route + optional held-source autofire. Old V2 operations retain
 * their ABI; this extension requires explicit support from the kernel. */
struct gkd_input_autofire_config { __u16 source, on_ms, off_ms, reserved; };
struct gkd_input_route_repeat_config {
    struct gkd_menu_vt_config route;
    struct gkd_input_autofire_config repeat;
};
#define GKD_INPUT_ROUTE_REPEAT _IOW('G', 0xe5, struct gkd_input_route_repeat_config)
#endif
