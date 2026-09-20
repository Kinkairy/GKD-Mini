/* SPDX-License-Identifier: GPL-2.0 */
/* Host shim runs verbatim VT routing/CONFIG/PULSE/CANCEL/release source blocks.
 * It does not simulate actual tty byte translation or kernel scheduling. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <linux/input.h>
#include "gkd-menu-vt.h"
#define BITS (sizeof(unsigned long)*8U)
#define WORDS ((KEY_CNT+BITS-1U)/BITS)
static unsigned long key_down[WORDS],gkd_system_blocked[WORDS];
static unsigned int gkd_menu_physical[KEY_CNT];
struct input_dev {unsigned long key[WORDS];int event_lock;};
struct input_handle {void *private;struct input_dev *dev;};
struct gkd_menu_vt_lease {struct gkd_menu_vt_config config;bool configured,pulse,pending;int error,timer,wait;};
static struct gkd_menu_vt_lease *gkd_menu_lease;
struct gkd_system_hotkey_lease {
 struct gkd_system_hotkey_config config;
 int timer;unsigned int native_target;
 bool configured,prefix_down,button_down,used,button_blocked,primed,native_pulse;
};
static struct gkd_system_hotkey_lease *gkd_system_lease;
struct file {void *private_data;};struct inode {int unused;};
static int test_bit(unsigned key,const unsigned long *bits){return !!(bits[key/BITS]&(1UL<<(key%BITS)));}
static void __assign_bit(unsigned key,unsigned long *bits,int value)
{if(value)bits[key/BITS]|=1UL<<(key%BITS);else bits[key/BITS]&=~(1UL<<(key%BITS));}
#define __set_bit(key,bits) __assign_bit(key,bits,1)
#define __clear_bit(key,bits) __assign_bit(key,bits,0)
static int events;static unsigned last_key;static int last_value;
static void kbd_keycode(unsigned key,int value,bool raw)
{(void)raw;assert(key<KEY_CNT);events++;last_key=key;last_value=value;if(value!=2)__assign_bit(key,key_down,value);}
#define kbd_is_hw_raw(x) false
#define spin_lock_irqsave(lock,flags) do {(flags)=0;} while(0)
#define spin_unlock_irqrestore(lock,flags) do {(void)(flags);} while(0)
#define spin_lock(lock) ((void)0)
#define spin_unlock(lock) ((void)0)
#define wake_up_interruptible(wait) ((void)0)
#define __user
#define copy_from_user(to,from,size) (memcpy(to,from,size),0)
#define HRTIMER_MODE_REL 0
#define ms_to_ktime(value) (value)
#define hrtimer_start(timer,time,mode) ((void)0)
static int system_cancel_check;
static void test_cancel(int *timer)
{
 if(system_cancel_check) {
  assert(gkd_system_lease && timer==&gkd_system_lease->timer);
  assert(!gkd_system_lease->configured); /* input can no longer re-arm */
 }
}
#define hrtimer_cancel(timer) test_cancel(timer)
#define kfree(value) free(value)
#define for_each_set_bit(key,bits,count) for((key)=0;(key)<(count);(key)++)if(test_bit((key),(bits)))
static void gkd_menu_wake_vt(void){}
#include "input-route-extracted.inc"
static struct gkd_menu_vt_config configuration(void)
{
 struct gkd_menu_vt_config c={0};c.version=GKD_MENU_VT_VERSION;c.hold_ms=100;c.trigger=KEY_HOME;
 c.count=1;c.keys[0]=KEY_SPACE;c.map_count=2;
 c.maps[0]=(struct gkd_input_route_map){KEY_LEFTCTRL,KEY_SPACE};
 c.maps[1]=(struct gkd_input_route_map){KEY_LEFTALT,KEY_SPACE};return c;
}
static struct file fresh(void)
{
 assert(!gkd_menu_lease);memset(key_down,0,sizeof(key_down));memset(gkd_menu_physical,0,sizeof(gkd_menu_physical));events=0;
 gkd_menu_lease=calloc(1,sizeof(*gkd_menu_lease));assert(gkd_menu_lease);
 return (struct file){gkd_menu_lease};
}
static void configure(struct file *f,struct gkd_menu_vt_config *c)
{assert(!gkd_menu_ioctl_locked(f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)c));}
static struct input_handle handle(struct input_dev *dev)
{struct input_handle h={calloc(WORDS,sizeof(unsigned long)),dev};assert(h.private);return h;}
static void key(struct input_handle *h,unsigned code,int value)
{if(value!=2)__assign_bit(code,h->dev->key,value);gkd_menu_key(h,code,value);}
int main(void)
{
 struct input_dev a={0},b={0};struct input_handle h=handle(&a),other=handle(&b);
 struct file f=fresh();struct gkd_menu_vt_config c=configuration();
 /* An unconfigured lease is exactly raw, including repeats and HOME. */
 key(&h,KEY_HOME,1);key(&h,KEY_HOME,2);key(&h,KEY_HOME,0);assert(events==3&&last_key==KEY_HOME);
 key(&h,KEY_LEFTCTRL,1);assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)&c)==-EBUSY);
 key(&h,KEY_LEFTCTRL,0);configure(&f,&c);events=0;
 key(&h,KEY_HOME,1);key(&h,KEY_HOME,0);assert(!events);
 key(&h,KEY_LEFTCTRL,1);assert(events==1&&last_key==KEY_SPACE&&last_value==1);
 key(&h,KEY_LEFTALT,1);assert(events==1);
 key(&h,KEY_LEFTCTRL,0);assert(events==1&&test_bit(KEY_SPACE,key_down));
 key(&h,KEY_LEFTALT,0);assert(events==2&&!test_bit(KEY_SPACE,key_down));
 /* Native target, remapped source and another device all retain ownership. */
 key(&h,KEY_SPACE,1);key(&h,KEY_LEFTCTRL,1);key(&other,KEY_LEFTCTRL,1);
 key(&h,KEY_SPACE,0);key(&h,KEY_LEFTCTRL,0);assert(test_bit(KEY_SPACE,key_down));
 gkd_menu_disconnect(&other);assert(!test_bit(KEY_SPACE,key_down));other.private=NULL;
 key(&h,KEY_LEFTCTRL,1);assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0)==-EBUSY);
 key(&h,KEY_LEFTCTRL,0);
 assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0));assert(test_bit(KEY_SPACE,key_down));
 assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0)==-EBUSY);
 key(&h,KEY_LEFTCTRL,1);int before=events;
 key(&h,KEY_LEFTCTRL,2);assert(events==before); /* pulse never repeats */
 gkd_menu_finish(gkd_menu_lease,0);assert(test_bit(KEY_SPACE,key_down)&&events==before&&gkd_menu_lease->pending);
 assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0)==-EBUSY);gkd_menu_lease->pending=false;
 key(&h,KEY_LEFTCTRL,2);assert(events==before+1&&last_value==2);
 key(&h,KEY_LEFTCTRL,0);assert(!test_bit(KEY_SPACE,key_down));
 assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0));
 assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CANCEL,0));assert(!test_bit(KEY_SPACE,key_down)&&gkd_menu_lease->error==ECANCELED);
 gkd_menu_lease->pending=false;
 /* Reconcile a dropped/grabbed physical release from the source snapshot. */
 key(&h,KEY_LEFTALT,1);__assign_bit(KEY_LEFTALT,a.key,0);gkd_menu_sync(&h);assert(!test_bit(KEY_SPACE,key_down));
 key(&h,KEY_LEFTCTRL,1);assert(!gkd_menu_release(NULL,&f));assert(!gkd_menu_lease&&!test_bit(KEY_SPACE,key_down));
 key(&h,KEY_LEFTCTRL,0);free(h.private);
 f=fresh();c=configuration();c.maps[1].source=c.maps[0].source;
 assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)&c)==-EINVAL);
 c=configuration();c.maps[2].target=1;assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)&c)==-EINVAL);
 c=configuration();c.maps[0].source=c.trigger;assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)&c)==-EINVAL);
 c=configuration();c.hold_ms=501;assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)(uintptr_t)&c)==-EINVAL);
 assert(gkd_menu_ioctl_locked(&f,0,0)==-ENOTTY);assert(!gkd_menu_release(NULL,&f));
 /* System MENU prefix uses the same real VT filter in raw and mapped modes.
  * Native L2 Pause is never involved in the screenshot sequence. */
 memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));h=handle(&a);other=handle(&b);
 f=fresh();
 struct gkd_system_hotkey_lease system={.config={GKD_SYSTEM_HOTKEY_VERSION,0,KEY_HOME,KEY_TAB},.configured=true};
 gkd_system_lease=&system;gkd_system_sync();events=0;
 key(&h,KEY_TAB,1);assert(events==1&&test_bit(KEY_TAB,key_down));
 key(&h,KEY_TAB,0);assert(events==2); /* shoulder has no timer or delay */
 key(&h,KEY_HOME,1);assert(events==2);
 key(&h,KEY_HOME,0);assert(events==3&&test_bit(KEY_HOME,key_down));
 gkd_system_finish(&system);assert(events==4&&last_key==KEY_HOME&&!test_bit(KEY_HOME,key_down));
 events=0;
 key(&h,KEY_HOME,1);key(&h,KEY_TAB,1);key(&h,KEY_TAB,2);assert(!events);
 key(&h,KEY_HOME,0);assert(!events);key(&h,KEY_TAB,0);assert(!events);
 key(&h,KEY_TAB,1);key(&h,KEY_TAB,0);assert(events==2);
 events=0;key(&h,KEY_TAB,1);key(&h,KEY_HOME,1);assert(events==1);
 key(&h,KEY_HOME,0);assert(events==2&&test_bit(KEY_TAB,key_down));
 gkd_system_finish(&system);assert(events==3);
 key(&h,KEY_TAB,0);assert(events==4); /* reverse order remains ordinary input */
 events=0;key(&h,KEY_HOME,1);key(&other,KEY_HOME,1);key(&h,KEY_HOME,0);assert(!events);
 key(&other,KEY_HOME,0);assert(events==1);gkd_system_finish(&system);assert(events==2);
 c=configuration();configure(&f,&c);events=0;
 key(&h,KEY_HOME,1);key(&h,KEY_HOME,0);assert(!events); /* route triggers own pulse */
 key(&h,KEY_HOME,1);key(&h,KEY_TAB,1);key(&h,KEY_TAB,0);key(&h,KEY_HOME,0);assert(!events);
 key(&h,KEY_TAB,1);key(&h,KEY_TAB,0);assert(events==2);
 events=0;key(&h,KEY_HOME,1);gkd_system_sync();key(&h,KEY_HOME,0);assert(!events);
 gkd_system_lease=NULL;assert(!gkd_menu_release(NULL,&f));
 free(h.private);free(other.private);
 struct gkd_system_hotkey_lease *owned=calloc(1,sizeof(*owned));assert(owned);
 gkd_system_lease=owned;struct file sf={owned};
 struct gkd_system_hotkey_config sc={GKD_SYSTEM_HOTKEY_VERSION,0,KEY_HOME,KEY_TAB};
 assert(gkd_system_ioctl(&sf,0,0)==-ENOTTY);
 sc.button=sc.prefix;assert(gkd_system_ioctl(&sf,GKD_SYSTEM_HOTKEY_CONFIG,(unsigned long)(uintptr_t)&sc)==-EINVAL);
 sc.button=KEY_TAB;assert(!gkd_system_ioctl(&sf,GKD_SYSTEM_HOTKEY_CONFIG,(unsigned long)(uintptr_t)&sc));
 assert(gkd_system_ioctl(&sf,GKD_SYSTEM_HOTKEY_CONFIG,(unsigned long)(uintptr_t)&sc)==-EALREADY);
 memset(&a,0,sizeof(a));h=handle(&a);events=0;
 key(&h,KEY_HOME,1);key(&h,KEY_HOME,0);assert(owned->native_pulse&&test_bit(KEY_HOME,key_down));
 system_cancel_check=1;
 assert(!gkd_system_release(NULL,&sf));assert(!gkd_system_lease&&!test_bit(KEY_HOME,key_down));
 system_cancel_check=0;
 key(&h,KEY_TAB,1);key(&h,KEY_TAB,0);assert(last_key==KEY_TAB&&last_value==0);free(h.private);
 /* Owner death must not replay held screenshot keys, including repeats. */
 owned=calloc(1,sizeof(*owned));assert(owned);gkd_system_lease=owned;sf.private_data=owned;
 assert(!gkd_system_ioctl(&sf,GKD_SYSTEM_HOTKEY_CONFIG,(unsigned long)(uintptr_t)&sc));
 memset(&a,0,sizeof(a));h=handle(&a);events=0;
 key(&h,KEY_HOME,1);key(&h,KEY_TAB,1);assert(!events);
 system_cancel_check=1;assert(!gkd_system_release(NULL,&sf));system_cancel_check=0;
 key(&h,KEY_HOME,2);key(&h,KEY_TAB,2);key(&h,KEY_HOME,0);key(&h,KEY_TAB,0);
 assert(!events&&!test_bit(KEY_HOME,gkd_system_blocked)&&!test_bit(KEY_TAB,gkd_system_blocked));
 key(&h,KEY_TAB,1);key(&h,KEY_TAB,0);assert(events==2);free(h.private);
 puts("GKD_INPUT_ROUTE_STATE=PASS actual-source raw/aliases/many-to-one/multi-device/pulse-overlap/repeat/cancel/sync/disconnect/close/invalid");
 return 0;
}
