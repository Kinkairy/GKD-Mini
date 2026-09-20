/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_SETTINGS_STATE_H
#define GKD_SETTINGS_STATE_H
#include "gkd-menu-state.h"
#include "gkd-settings-values.h"
#include "gkd-input-style.h"
#define GKD_SETTINGS_ROWS 6U
#define GKD_SETTINGS_UPDATE_ROW 5U
struct gkd_settings_values {
    unsigned animation, sleep_minutes, show_fps, chinese, input_style;
};
struct gkd_settings_state {
    struct gkd_menu_state input;
    struct gkd_settings_values original, draft;
};
int gkd_settings_state_init(struct gkd_settings_state *state,
                            const struct gkd_settings_values *values);
/* Uses the shared held-key/drop barrier. A saves, B cancels; neither adjusts. */
int gkd_settings_state_key(struct gkd_settings_state *state,
                           unsigned source, unsigned key, int value);
/* Only a completed A result exposes draft values; cancellation returns original. */
int gkd_settings_state_result(const struct gkd_settings_state *state,
                              struct gkd_settings_values *values);
#endif
