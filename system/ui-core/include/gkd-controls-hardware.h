/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_CONTROLS_HARDWARE_H
#define GKD_CONTROLS_HARDWARE_H
#include <sound/asound.h>
struct gkd_controls_hardware {
    int control_fd, brightness_fd;
    struct snd_ctl_elem_id pcm;
    int logical_volume, raw_volume;
};
/* Borrow fixed board descriptors; startup only reads. No defaults or OSS route. */
int gkd_controls_hardware_init(struct gkd_controls_hardware *hw, int ctl, int brightness, int maximum);
int gkd_controls_volume_read(struct gkd_controls_hardware *hw, int *logical);
int gkd_controls_volume_set(struct gkd_controls_hardware *hw, unsigned target, int *logical);
int gkd_controls_brightness_set(struct gkd_controls_hardware *hw, unsigned target, int *percent);
int gkd_controls_volume_adjust(struct gkd_controls_hardware *hw, int delta, int *logical);
int gkd_controls_brightness_read(struct gkd_controls_hardware *hw, int *percent);
int gkd_controls_brightness_cycle(struct gkd_controls_hardware *hw, const unsigned *steps, unsigned count, int *percent);
#endif
