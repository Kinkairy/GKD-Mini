/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-settings-state.h"
int gkd_settings_state_init(struct gkd_settings_state *s, const struct gkd_settings_values *v)
{
    if (!s || !v || v->animation > 1U || !gkd_settings_sleep_valid(v->sleep_minutes) ||
        v->show_fps > 1U || v->chinese > 1U || v->input_style >= GKD_INPUT_STYLE_COUNT) return -1;
    if (gkd_menu_state_init(&s->input, GKD_SETTINGS_ROWS)) return -1;
    s->original = *v; s->draft = *v;
    return 0;
}
int gkd_settings_state_key(struct gkd_settings_state *s, unsigned source, unsigned key, int value)
{
    unsigned *target; int changed;
    if (!s) return -1;
    changed = gkd_menu_state_key(&s->input, source, key, value);
    if (changed <= 0 || !s->input.adjustment) return changed;
    if (s->input.selected == GKD_SETTINGS_UPDATE_ROW) return 0;
    if (s->input.selected == 1U) {
        unsigned old = s->draft.sleep_minutes;
        s->draft.sleep_minutes = gkd_settings_sleep_step(old,s->input.adjustment);
        return old != s->draft.sleep_minutes;
    }
    if (s->input.selected == 4U) {
        if (value != 1) return 0;
        unsigned old = s->draft.input_style;
        if (s->input.adjustment < 0 && old) s->draft.input_style--;
        if (s->input.adjustment > 0 && old + 1U < GKD_INPUT_STYLE_COUNT) s->draft.input_style++;
        return old != s->draft.input_style;
    }
    target = s->input.selected == 0U ? &s->draft.animation :
             s->input.selected == 2U ? &s->draft.show_fps : &s->draft.chinese;
    /* Binary choices toggle once per press, never auto-repeat. */
    if (value != 1) return 0;
    *target ^= 1U;
    return 1;
}
int gkd_settings_state_result(const struct gkd_settings_state *s, struct gkd_settings_values *v)
{
    if (!s || !v || s->input.result == GKD_MENU_PENDING) return -1;
    *v = s->input.result == GKD_MENU_SELECTED ? s->draft : s->original;
    return 0;
}
