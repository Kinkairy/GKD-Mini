/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-menu-state.h"
#include <assert.h>
#include <stdio.h>
static void fresh(struct gkd_menu_state *s)
{
 assert(!gkd_menu_state_init(s,3)); assert(!gkd_menu_state_snapshot(s,0,0)); assert(!gkd_menu_state_snapshot(s,1,0));
}
int main(void)
{
 struct gkd_menu_state s; unsigned source,key;
 assert(gkd_menu_state_init(NULL,3)<0);assert(gkd_menu_state_init(&s,0)<0);
 for(source=0;source<2;source++) {
  fresh(&s); assert(gkd_menu_state_key(&s,source,GKD_MENU_UP,1)==1);assert(s.selected==2);
  assert(gkd_menu_state_key(&s,source,GKD_MENU_UP,2)==1);assert(s.selected==1);
  assert(!gkd_menu_state_key(&s,source,GKD_MENU_UP,0));
  assert(gkd_menu_state_key(&s,source,GKD_MENU_DOWN,1)==1);assert(s.selected==2);
  assert(!gkd_menu_state_key(&s,source,GKD_MENU_DOWN,1));
  assert(gkd_menu_state_key(&s,source,GKD_MENU_DOWN,2)==1);assert(s.selected==0);
  assert(!gkd_menu_state_key(&s,source,GKD_MENU_DOWN,0));
  assert(gkd_menu_state_key(&s,source,GKD_MENU_CONFIRM,1)==1);assert(s.result==GKD_MENU_SELECTED);
  assert(!gkd_menu_state_key(&s,source,GKD_MENU_CANCEL,1));assert(s.result==GKD_MENU_SELECTED);
  fresh(&s);assert(!gkd_menu_state_key(&s,source,GKD_MENU_DOWN,2));assert(s.selected==0);
  assert(gkd_menu_state_key(&s,source,GKD_MENU_CANCEL,1)==1);assert(s.result==GKD_MENU_CANCELLED);
  for(key=0;key<4;key++) {
   fresh(&s);assert(!gkd_menu_state_snapshot(&s,source,1U<<key));
   assert(!gkd_menu_state_key(&s,source,key,1)); assert(!gkd_menu_state_key(&s,source,key,2));
   assert(s.result==GKD_MENU_PENDING && s.selected==0);
   assert(!gkd_menu_state_key(&s,source,key,0));assert(!s.inhibited);
   assert(gkd_menu_state_key(&s,source,key,1)==1);
  }
 }
 fresh(&s);assert(gkd_menu_state_key(&s,0,GKD_MENU_DOWN,1)==1);
 assert(!gkd_menu_state_key(&s,1,GKD_MENU_DOWN,1));assert(s.selected==1);
 assert(!gkd_menu_state_key(&s,0,GKD_MENU_DOWN,0));assert(!gkd_menu_state_key(&s,1,GKD_MENU_DOWN,1));
 assert(!gkd_menu_state_key(&s,1,GKD_MENU_DOWN,0));assert(gkd_menu_state_key(&s,1,GKD_MENU_DOWN,1)==1);assert(s.selected==2);
 fresh(&s);assert(!gkd_menu_state_drop(&s,0));assert(!gkd_menu_state_key(&s,0,GKD_MENU_CONFIRM,1));
 assert(!gkd_menu_state_key(&s,1,GKD_MENU_CONFIRM,1));assert(s.result==GKD_MENU_PENDING);
 assert(!gkd_menu_state_snapshot(&s,0,0));assert(s.inhibited);
 assert(!gkd_menu_state_key(&s,1,GKD_MENU_CONFIRM,0));assert(!s.inhibited);
 assert(gkd_menu_state_key(&s,0,GKD_MENU_CONFIRM,1)==1);
 fresh(&s);assert(gkd_menu_state_key(&s,2,0,1)<0);assert(gkd_menu_state_key(&s,0,GKD_MENU_KEY_COUNT,1)<0);
 assert(gkd_menu_state_key(&s,0,0,3)<0);assert(gkd_menu_state_snapshot(&s,0,1U<<GKD_MENU_KEY_COUNT)<0);
 puts("MENU_STATE_PASS navigation/repeat/wrap/held-entry/two-source/drop/rearm/terminal");return 0;
}
