/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_MENU_STATE_H
#define GKD_MENU_STATE_H
/* Pure logical input state shared by the production runner and host tests. */
enum gkd_menu_key { GKD_MENU_UP, GKD_MENU_DOWN, GKD_MENU_CONFIRM, GKD_MENU_CANCEL, GKD_MENU_LEFT, GKD_MENU_RIGHT, GKD_MENU_KEY_COUNT };
enum gkd_menu_result { GKD_MENU_PENDING, GKD_MENU_SELECTED, GKD_MENU_CANCELLED };
struct gkd_menu_state {
    unsigned count, selected, held[2], dropped[2];
    int inhibited;
    int scroll_only; /* Text viewport clamps at both ends, without menu selection. */
    int cancel_blocked; /* Automatic USB entry requires an explicit mode. */
    int adjustment; /* Last accepted left/right event: -1 or +1, otherwise 0. */
    enum gkd_menu_result result;
};
int gkd_menu_state_init(struct gkd_menu_state *s, unsigned count);
int gkd_menu_state_snapshot(struct gkd_menu_state *s, unsigned source, unsigned held);
int gkd_menu_state_drop(struct gkd_menu_state *s, unsigned source);
/* 1: selection/result changed; 0: consumed; -1: invalid input. */
int gkd_menu_state_key(struct gkd_menu_state *s, unsigned source, unsigned key, int value);
#endif
