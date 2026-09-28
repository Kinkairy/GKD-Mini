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
#include <stddef.h>
#include <linux/input.h>
#include "gkd-menu-vt.h"
#define BITS (sizeof(unsigned long)*8U)
#define WORDS ((KEY_CNT+BITS-1U)/BITS)
static unsigned long key_down[WORDS],gkd_system_blocked[WORDS];
static unsigned int gkd_menu_physical[KEY_CNT];
struct input_dev {unsigned long key[WORDS];int event_lock;};
struct input_handle {void *private;struct input_dev *dev;};
enum hrtimer_restart { HRTIMER_NORESTART, HRTIMER_RESTART };
static long fake_now;
struct hrtimer {int delay,armed;long expires;};
#define ktime_get() fake_now
#define ktime_before(a,b) ((a)<(b))
#define hrtimer_get_expires(timer) ((timer)->expires)
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
struct gkd_menu_vt_lease {
 struct gkd_menu_vt_config config;
 struct hrtimer timer,autofire_timer;
 struct gkd_input_autofire_config repeat;
 bool autofire_held,autofire_phase,autofire_paused;
 bool configured,pulse,pending;int error,wait;
};
static struct gkd_menu_vt_lease *gkd_menu_lease;
struct gkd_system_hotkey_lease {
 struct gkd_system_hotkey_config config;
 struct hrtimer timer;unsigned int native_target;
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
#define hrtimer_start(timer,time,mode) ((timer)->delay=(time),(timer)->expires=fake_now+(time),(timer)->armed=1)
#define hrtimer_forward_now(timer,time) ((timer)->delay=(time),(timer)->expires=fake_now+(time))

static int system_cancel_check;
static void test_cancel(struct hrtimer *timer)
{
 timer->armed=0;
 if(gkd_menu_lease&&timer==&gkd_menu_lease->autofire_timer)assert(!gkd_menu_lease->repeat.source);
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
static void route_updates(void)
{
 struct input_dev dev={0};struct input_handle h=handle(&dev);
 struct file f=fresh();struct gkd_menu_vt_config raw=configuration();
 raw.map_count=0;memset(raw.maps,0,sizeof(raw.maps));
 struct gkd_menu_vt_config portrait=raw;
 portrait.map_count=1;portrait.maps[0]=(struct gkd_input_route_map){KEY_LEFTCTRL,KEY_SPACE};
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait)==-ENXIO);
 configure(&f,&raw);
 key(&h,KEY_LEFTCTRL,1);
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait)==-EBUSY);
 assert(!memcmp(&gkd_menu_lease->config,&raw,sizeof(raw)));
 key(&h,KEY_LEFTCTRL,2);assert(last_key==KEY_LEFTCTRL&&last_value==2);
 key(&h,KEY_LEFTCTRL,0);assert(last_key==KEY_LEFTCTRL&&last_value==0);
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait));
 key(&h,KEY_LEFTCTRL,1);assert(last_key==KEY_SPACE&&last_value==1);
 key(&h,KEY_SPACE,1);int count=events;
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait));
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&raw)==-EBUSY);
 key(&h,KEY_LEFTCTRL,0);assert(events==count&&test_bit(KEY_SPACE,key_down));
 key(&h,KEY_SPACE,0);assert(last_key==KEY_SPACE&&last_value==0);
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&raw));
 key(&h,KEY_LEFTCTRL,1);assert(last_key==KEY_LEFTCTRL&&last_value==1);key(&h,KEY_LEFTCTRL,0);
 assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0));
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait)==-EBUSY);
 gkd_menu_finish(gkd_menu_lease,0);
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait)==-EBUSY);
 gkd_menu_lease->pending=false;
 struct gkd_menu_vt_config bad=portrait;bad.trigger=KEY_ESC;
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&bad)==-EPERM);
 bad=portrait;bad.keys[0]=KEY_ENTER;
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&bad)==-EPERM);
 bad=portrait;bad.maps[1]=bad.maps[0];bad.map_count=2;
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&bad)==-EINVAL);
 assert(!memcmp(&gkd_menu_lease->config,&raw,sizeof(raw)));
 assert(gkd_menu_ioctl_locked(&f,GKD_MENU_VT_CONFIG,(unsigned long)&portrait)==-EALREADY);
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_UPDATE,(unsigned long)&portrait));
 key(&h,KEY_LEFTCTRL,1);assert(test_bit(KEY_SPACE,key_down));
 assert(!gkd_menu_release(NULL,&f));assert(!test_bit(KEY_SPACE,key_down));
 key(&h,KEY_LEFTCTRL,0);events=0;
 /* After game-owner close, the next SimpleMenu A/dot is native again. */
 key(&h,KEY_LEFTCTRL,1);assert(events==1&&last_key==KEY_LEFTCTRL&&last_value==1);
 key(&h,KEY_LEFTCTRL,0);free(h.private);
 puts("GKD_PORTRAIT_KERNEL=PASS update/hold/repeat/shared-target/menu/invalid/close-to-native");
}
static enum hrtimer_restart autofire_tick(struct hrtimer *timer)
{fake_now=timer->expires;return gkd_autofire_timeout(timer);}
static void autofire_cases(void)
{
 struct input_dev dev={0};struct input_handle h=handle(&dev);
 struct file f=fresh();struct gkd_menu_vt_config base=configuration();
 base.map_count=0;memset(base.maps,0,sizeof(base.maps));configure(&f,&base);
 struct gkd_input_route_repeat_config req={.route=base,.repeat={KEY_LEFTCTRL,50,50,0}};
 req.route.map_count=1;req.route.maps[0]=(struct gkd_input_route_map){KEY_LEFTCTRL,KEY_LEFTSHIFT};
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&req));
 key(&h,KEY_LEFTCTRL,1);assert(events==1&&last_key==42&&last_value==1&&gkd_menu_lease->autofire_timer.delay==50);
 int n=events;key(&h,KEY_LEFTCTRL,2);assert(events==n); /* no OS-repeat events */
 for(int i=0;i<20;i++){
  assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_RESTART);
  assert(last_key==42&&last_value==(i%2)&&gkd_menu_lease->autofire_timer.delay==50);
 }
 assert(events==21);key(&h,KEY_LEFTCTRL,0);assert(events==22&&last_value==0);
 n=events;assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_NORESTART&&events==n);
 /* An already-expired callback waiting on the keyboard lock must not
  * toggle a newly rearmed press ahead of its deadline. */
 key(&h,KEY_LEFTCTRL,1);key(&h,KEY_LEFTCTRL,0);fake_now+=10;
 key(&h,KEY_LEFTCTRL,1);n=events;
 assert(gkd_autofire_timeout(&gkd_menu_lease->autofire_timer)==HRTIMER_RESTART&&events==n&&test_bit(42,key_down));
 key(&h,KEY_LEFTCTRL,0);
 /* Two input producers can hold the same source. Releasing one does not
  * stop the remaining holder's timed output. */
 struct input_dev dev2={0};struct input_handle second=handle(&dev2);
 key(&h,KEY_LEFTCTRL,1);key(&second,KEY_LEFTCTRL,1);key(&h,KEY_LEFTCTRL,0);
 assert(gkd_menu_lease->autofire_held);
 assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_RESTART);
 key(&second,KEY_LEFTCTRL,0);
 assert(!gkd_menu_lease->autofire_held);free(second.private);
 /* Native Y owns the output throughout OFF phases; the repeating source
  * cannot release a separately held Y. */
 key(&h,KEY_LEFTSHIFT,1);key(&h,KEY_LEFTCTRL,1);n=events;
 assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_RESTART);
 assert(events==n&&test_bit(42,key_down));
 key(&h,KEY_LEFTSHIFT,0);assert(last_value==0&&!test_bit(42,key_down));
 assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_RESTART&&test_bit(42,key_down));
 key(&h,KEY_LEFTCTRL,0);
 /* Loss of orientation stops repetition now, even while the map reset
  * waits for the held source to be released. */
 key(&h,KEY_LEFTCTRL,1);
 struct gkd_input_route_repeat_config off={.route=base};
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&off)==-EBUSY);
 assert(!test_bit(42,key_down)&&gkd_menu_lease->autofire_paused);
 n=events;assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_NORESTART&&events==n);
 key(&h,KEY_LEFTCTRL,2);assert(events==n);
 key(&h,KEY_LEFTCTRL,0);
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&off));
 key(&h,KEY_LEFTCTRL,1);assert(last_key==29&&last_value==1);key(&h,KEY_LEFTCTRL,0);
 /* Invalid rates / consumed source / reserved bytes never alter the lease. */
 struct gkd_input_route_repeat_config bad=req;bad.repeat.on_ms=19;
 assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&bad)==-EINVAL);
 bad=req;bad.repeat.off_ms=501;assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&bad)==-EINVAL);
 bad=req;bad.repeat.reserved=1;assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&bad)==-EINVAL);
 bad=req;bad.repeat.source=0;assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&bad)==-EINVAL);
 bad=req;bad.route.maps[0].target=0;assert(gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&bad)==-EINVAL);
 assert(!gkd_menu_lease->repeat.source);
 assert(!gkd_menu_ioctl_locked(&f,GKD_INPUT_ROUTE_REPEAT,(unsigned long)&req));
 /* Dropped physical release and disconnect both stop the timer stream. */
 key(&h,KEY_LEFTCTRL,1);__clear_bit(KEY_LEFTCTRL,dev.key);gkd_menu_sync(&h);
 assert(!test_bit(42,key_down)&&!gkd_menu_lease->autofire_held);
 assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_NORESTART);
 key(&h,KEY_LEFTCTRL,1);gkd_menu_disconnect(&h);
 assert(!test_bit(42,key_down)&&!gkd_menu_lease->autofire_held);
 assert(autofire_tick(&gkd_menu_lease->autofire_timer)==HRTIMER_NORESTART);
 memset(&dev,0,sizeof(dev));h=handle(&dev);key(&h,KEY_LEFTCTRL,1);
 assert(!gkd_menu_release(NULL,&f)&&!test_bit(42,key_down));
 key(&h,KEY_LEFTCTRL,0);n=events;
 key(&h,KEY_LEFTCTRL,1);assert(events==n+1&&last_key==29&&last_value==1);key(&h,KEY_LEFTCTRL,0);free(h.private);
 /* Exercise the pre-existing MENU timer callback too, with the same shim. */
 f=fresh();configure(&f,&base);assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0));
 assert(gkd_menu_timeout(&gkd_menu_lease->timer)==HRTIMER_NORESTART&&!gkd_menu_lease->pulse);
 assert(!gkd_menu_release(NULL,&f));
 puts("GKD_AUTOFIRE_KERNEL=PASS 10Hz/down-up/release/native-Y/no-OS-repeat/unknown/invalid/drop/disconnect/close/menu-timer");
}
int main(void)
{
 autofire_cases();
 route_updates();
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
 /* Raw filters brightness and reserves HOME for a MENU-only pulse. Game
  * buttons remain unmapped, while the controls observer sees KEY_END. */
 memset(&a,0,sizeof(a));h=handle(&a);f=fresh();memset(&c,0,sizeof(c));
 c.version=GKD_MENU_VT_VERSION;c.hold_ms=100;c.trigger=KEY_HOME;
 c.count=1;c.keys[0]=KEY_ESC;
 c.map_count=1;c.maps[0]=(struct gkd_input_route_map){KEY_END,0};
 configure(&f,&c);events=0;
 key(&h,KEY_END,1);key(&h,KEY_END,2);key(&h,KEY_END,0);assert(!events);
 key(&h,KEY_HOME,1);key(&h,KEY_HOME,0);assert(!events);
 assert(!gkd_menu_ioctl_locked(&f,GKD_MENU_VT_PULSE,0));
 assert(events==1&&last_key==KEY_ESC&&last_value==1);
 gkd_menu_finish(gkd_menu_lease,0);assert(events==2&&last_key==KEY_ESC&&last_value==0);
 gkd_menu_lease->pending=false;
 assert(!gkd_menu_release(NULL,&f));free(h.private);
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
