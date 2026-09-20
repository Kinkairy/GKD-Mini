/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_HARDWARE_STATE_H
#define GKD_HARDWARE_STATE_H

struct gkd_hardware_state {
	unsigned int volume;
	unsigned int brightness;
};

int gkd_hardware_state_load(int dirfd, struct gkd_hardware_state *state);
int gkd_hardware_state_save(int dirfd, const struct gkd_hardware_state *state);

#endif
