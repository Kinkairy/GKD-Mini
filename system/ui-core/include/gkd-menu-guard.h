/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_MENU_GUARD_H
#define GKD_MENU_GUARD_H

#include <stdint.h>

struct gkd_menu_guard_owner { int lease_fd; };
struct gkd_menu_guard_controls { int lease_fd; uint64_t epoch; };

void gkd_menu_guard_owner_init(struct gkd_menu_guard_owner *owner);
int gkd_menu_guard_owner_enter(struct gkd_menu_guard_owner *owner);
void gkd_menu_guard_owner_exit(struct gkd_menu_guard_owner *owner);
void gkd_menu_guard_controls_init(struct gkd_menu_guard_controls *guard);
/* Returns 0 with a shared lease and epoch, 1 if an owner is active, or -1. */
int gkd_menu_guard_controls_acquire(struct gkd_menu_guard_controls *guard,
	uint64_t *epoch_out);
void gkd_menu_guard_controls_release(struct gkd_menu_guard_controls *guard);

#endif
