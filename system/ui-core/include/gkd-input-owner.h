// SPDX-License-Identifier: GPL-2.0
#ifndef GKD_INPUT_OWNER_H
#define GKD_INPUT_OWNER_H

#include "gkd-menu-guard.h"

struct gkd_input_owner {
	int physical_fd;
	int virtual_fd;
	int physical_grabbed;
	int virtual_grabbed;
	int marker_published;
	struct gkd_menu_guard_owner menu_guard;
};

void gkd_input_owner_init(struct gkd_input_owner *owner);
/* Nonexclusive observation: no EVIOCGRAB and no menu ownership marker. */
int gkd_input_observer_open(struct gkd_input_owner *owner);
void gkd_input_observer_close(struct gkd_input_owner *owner);
/* Production menu ownership: exclusive grabs without test-only markers. */
int gkd_input_owner_open_menu(struct gkd_input_owner *owner);
/* Prepare exclusive input while a caller-owned menu guard remains live. */
int gkd_input_owner_open_menu_reuse(struct gkd_input_owner *owner,
	struct gkd_menu_guard_owner *guard);
/* Transfer that live guard only after the complete consumer is ready. */
int gkd_input_owner_transfer_menu_guard(struct gkd_input_owner *owner,
	struct gkd_menu_guard_owner *guard);
/* Frozen recovery/test interface: requires and publishes the test marker. */
int gkd_input_owner_open(struct gkd_input_owner *owner);
int gkd_input_owner_next_key(struct gkd_input_owner *owner);
/* Returns 0 at the monotonic deadline; releases and EINTR do not extend it. */
int gkd_input_owner_next_key_timeout(struct gkd_input_owner *owner, int timeout_ms);
int gkd_input_owner_close(struct gkd_input_owner *owner);

#endif
