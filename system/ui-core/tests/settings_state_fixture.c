/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-settings-state.h"
#include <assert.h>
#include <stdio.h>
static void fresh(struct gkd_settings_state *s)
{
    const struct gkd_settings_values v = {1,0,0,0,0};
    assert(!gkd_settings_state_init(s,&v));
    assert(!gkd_menu_state_snapshot(&s->input,0,0));
    assert(!gkd_menu_state_snapshot(&s->input,1,0));
}
static void tap(struct gkd_settings_state *s, unsigned key)
{
    assert(gkd_settings_state_key(s,0,key,1)>=0);
    assert(!gkd_settings_state_key(s,0,key,0));
}
int main(void)
{
    struct gkd_settings_state s; struct gkd_settings_values v;
    fresh(&s);
    assert(gkd_settings_state_result(&s,&v)<0);
    tap(&s,GKD_MENU_RIGHT); assert(!s.draft.animation);
    assert(gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,1)==1);
    assert(s.draft.animation==1);
    for(unsigned i=0;i<8;i++) assert(!gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,2));
    assert(s.draft.animation==1);
    assert(!gkd_settings_state_key(&s,1,GKD_MENU_RIGHT,1)); /* duplicate source */
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,0));
    assert(!gkd_settings_state_key(&s,1,GKD_MENU_RIGHT,0));
    tap(&s,GKD_MENU_DOWN); assert(s.input.selected==1);
    tap(&s,GKD_MENU_LEFT); assert(!s.draft.sleep_minutes);
    assert(gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,1)==1);
    for(unsigned i=0;i<100;i++) assert(gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,2)>=0);
    assert(s.draft.sleep_minutes==60);
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_LEFT,1)); /* opposing hold */
    assert(s.draft.sleep_minutes==60);
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_LEFT,0));
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,0));
    tap(&s,GKD_MENU_LEFT); assert(s.draft.sleep_minutes==30);
    tap(&s,GKD_MENU_DOWN); tap(&s,GKD_MENU_RIGHT); assert(s.draft.show_fps==1);
    tap(&s,GKD_MENU_DOWN); tap(&s,GKD_MENU_LEFT); assert(s.draft.chinese==1);
    tap(&s,GKD_MENU_CONFIRM);
    assert(!gkd_settings_state_result(&s,&v));
    assert(v.animation==1 && v.sleep_minutes==30 && v.show_fps==1 && v.chinese==1);
    tap(&s,GKD_MENU_RIGHT); assert(s.draft.chinese==1); /* terminal */
    fresh(&s);tap(&s,GKD_MENU_RIGHT);tap(&s,GKD_MENU_CANCEL);
    assert(!gkd_settings_state_result(&s,&v));
    assert(v.animation==1 && !v.sleep_minutes && !v.show_fps && !v.chinese);
    for(unsigned key=0;key<GKD_MENU_KEY_COUNT;key++){
        fresh(&s);assert(!gkd_menu_state_snapshot(&s.input,0,1U<<key));
        assert(!gkd_settings_state_key(&s,0,key,1));
        assert(!gkd_settings_state_key(&s,0,key,2));
        assert(s.draft.animation==1 && s.input.result==GKD_MENU_PENDING);
        assert(!gkd_settings_state_key(&s,0,key,0));
        assert(!s.input.inhibited);
    }
    fresh(&s);assert(!gkd_menu_state_drop(&s.input,0));
    assert(!gkd_settings_state_key(&s,1,GKD_MENU_RIGHT,1));assert(s.draft.animation==1);
    assert(!gkd_menu_state_snapshot(&s.input,0,0));assert(s.input.inhibited);
    assert(!gkd_settings_state_key(&s,1,GKD_MENU_RIGHT,0));assert(!s.input.inhibited);
    tap(&s,GKD_MENU_RIGHT);assert(!s.draft.animation);
    
    fresh(&s);tap(&s,GKD_MENU_DOWN);
    const unsigned up[]={5,10,15,30,60,60},down[]={30,15,10,5,0,0};
    for(unsigned i=0;i<6U;i++){tap(&s,GKD_MENU_RIGHT);assert(s.draft.sleep_minutes==up[i]);}
    for(unsigned i=0;i<6U;i++){tap(&s,GKD_MENU_LEFT);assert(s.draft.sleep_minutes==down[i]);}
    for(unsigned minutes=0;minutes<=61U;minutes++){
        v=(struct gkd_settings_values){1,minutes,0,0,0};
        int allowed=minutes==0||minutes==5||minutes==10||minutes==15||minutes==30||minutes==60;
        assert((gkd_settings_state_init(&s,&v)==0)==allowed);
    }

    fresh(&s);s.input.selected=4U;
    tap(&s,GKD_MENU_LEFT);assert(s.draft.input_style==GKD_INPUT_RAW);
    tap(&s,GKD_MENU_RIGHT);assert(s.draft.input_style==GKD_INPUT_XBOX);
    assert(gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,1)==1);
    assert(s.draft.input_style==GKD_INPUT_PS);
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,2));
    assert(!gkd_settings_state_key(&s,0,GKD_MENU_RIGHT,0));
    tap(&s,GKD_MENU_RIGHT);assert(s.draft.input_style==GKD_INPUT_PS);
    tap(&s,GKD_MENU_CANCEL);assert(!gkd_settings_state_result(&s,&v)&&v.input_style==GKD_INPUT_RAW);
    fresh(&s);s.input.selected=4U;tap(&s,GKD_MENU_RIGHT);tap(&s,GKD_MENU_CONFIRM);
    assert(!gkd_settings_state_result(&s,&v)&&v.input_style==GKD_INPUT_XBOX);
    fresh(&s);s.input.selected=GKD_SETTINGS_UPDATE_ROW;
    struct gkd_settings_values unchanged=s.draft;
    tap(&s,GKD_MENU_LEFT);tap(&s,GKD_MENU_RIGHT);
    assert(s.draft.animation==unchanged.animation&&s.draft.chinese==unchanged.chinese&&s.draft.input_style==unchanged.input_style);
    tap(&s,GKD_MENU_CONFIRM);assert(s.input.result==GKD_MENU_SELECTED&&s.input.selected==GKD_SETTINGS_UPDATE_ROW);
    fresh(&s);tap(&s,GKD_MENU_UP);assert(s.input.selected==GKD_SETTINGS_UPDATE_ROW);
    tap(&s,GKD_MENU_CANCEL);assert(s.input.result==GKD_MENU_CANCELLED);
    v.input_style=3U;assert(gkd_settings_state_init(&s,&v)<0);
    puts("SETTINGS_STYLE_PASS raw-default/three-values/no-repeat/save/cancel/invalid");
    puts("SETTINGS_STATE_PASS limits/six-step-ladder/repeat/duplicate/opposed/save/cancel/held/drop");
    return 0;
}
