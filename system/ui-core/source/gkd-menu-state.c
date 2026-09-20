/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-menu-state.h"
#include <string.h>
int gkd_menu_state_init(struct gkd_menu_state *s, unsigned count)
{
    if (!s || !count || count > 32U) return -1;
    memset(s, 0, sizeof(*s)); s->count = count; s->inhibited = 1;
    /* Both device snapshots must succeed before any press is actionable. */
    s->dropped[0] = s->dropped[1] = 1U;
    return 0;
}
int gkd_menu_state_snapshot(struct gkd_menu_state *s, unsigned source, unsigned held)
{
    if (!s || source > 1U || held >= (1U << GKD_MENU_KEY_COUNT)) return -1;
    s->held[source] = held; s->dropped[source] = 0;
    s->inhibited = !!(s->held[0] | s->held[1] | s->dropped[0] | s->dropped[1]);
    return 0;
}
int gkd_menu_state_drop(struct gkd_menu_state *s, unsigned source)
{
    if (!s || source > 1U) return -1;
    s->dropped[source] = 1U; s->inhibited = 1; return 0;
}
int gkd_menu_state_key(struct gkd_menu_state *s, unsigned source, unsigned key, int value)
{
    unsigned bit, was, own;
    if (!s || source > 1U || key >= GKD_MENU_KEY_COUNT || value < 0 || value > 2) return -1;
    s->adjustment = 0;
    if (s->dropped[source]) return 0;
    bit = 1U << key; was = (s->held[0] | s->held[1]) & bit; own = s->held[source] & bit;
    if (!value) s->held[source] &= ~bit;
    else if (value == 1) s->held[source] |= bit;
    if (s->inhibited) {
        if (!(s->held[0] | s->held[1] | s->dropped[0] | s->dropped[1])) s->inhibited = 0;
        return 0;
    }
    if (!value || s->result != GKD_MENU_PENDING ||
        (value == 1 && was) || (value == 2 && !own)) return 0;
    if (key == GKD_MENU_UP) s->selected = s->selected ? s->selected - 1U : s->scroll_only ? 0U : s->count - 1U;
    else if (key == GKD_MENU_DOWN) s->selected = s->scroll_only && s->selected + 1U == s->count ? s->selected : (s->selected + 1U) % s->count;
    else if (key == GKD_MENU_LEFT || key == GKD_MENU_RIGHT) {
        unsigned opposite = 1U << (key == GKD_MENU_LEFT ? GKD_MENU_RIGHT : GKD_MENU_LEFT);
        if ((s->held[0] | s->held[1]) & opposite) return 0;
        s->adjustment = key == GKD_MENU_LEFT ? -1 : 1;
    }
    else if (key == GKD_MENU_CANCEL && s->cancel_blocked) return 0;
    else if (value == 1) s->result = key == GKD_MENU_CONFIRM ? GKD_MENU_SELECTED : GKD_MENU_CANCELLED;
    else return 0;
    return 1;
}
