/* SPDX-License-Identifier: GPL-2.0 */
/* Included after kbd_keycode. All key ownership and injection are serialized
 * by the VT's existing kbd_event_lock. No emulator identity lives here. */
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/poll.h>
#include <linux/capability.h>
#include <linux/mutex.h>
#include <linux/gkd-menu-vt.h>
struct gkd_menu_vt_lease {
 struct gkd_menu_vt_config config;
 struct hrtimer timer, autofire_timer;
 struct gkd_input_autofire_config repeat;
 bool autofire_held, autofire_phase, autofire_paused;
 wait_queue_head_t wait;
 struct mutex operation;
 bool configured, pulse, pending;
 int error;
};
static struct gkd_menu_vt_lease *gkd_menu_lease;
struct gkd_system_hotkey_lease {
 struct gkd_system_hotkey_config config;
 struct hrtimer timer;
 unsigned int native_target;
 bool configured, prefix_down, button_down, used, button_blocked, primed, native_pulse;
};
static struct gkd_system_hotkey_lease *gkd_system_lease;
/* Consumed holds stay consumed across owner close until physical release. */
static DECLARE_BITMAP(gkd_system_blocked, KEY_CNT);
static unsigned int gkd_menu_physical[KEY_CNT];
static void gkd_menu_wake_vt(void)
{
 tasklet_schedule(&keyboard_tasklet);
 do_poke_blanked_console = 1;
 schedule_console_callback();
}
static bool gkd_menu_target(unsigned int key)
{
 unsigned int i;
 if (!gkd_menu_lease || !gkd_menu_lease->configured)
  return false;
 for (i = 0; i < gkd_menu_lease->config.count; i++)
  if (gkd_menu_lease->config.keys[i] == key)
   return true;
 return false;
}
/* A configured lease routes every keyboard source; unmapped sources retain
 * their native code. Target zero consumes a source. Pulses name final native
 * codes and are not remapped for a second time. */
static unsigned int gkd_input_destination(unsigned int source)
{
 unsigned int i;
 if (!gkd_menu_lease || !gkd_menu_lease->configured) return source;
 if (source == gkd_menu_lease->config.trigger) return 0;
 for (i = 0; i < gkd_menu_lease->config.map_count; i++)
  if (gkd_menu_lease->config.maps[i].source == source)
   return gkd_menu_lease->config.maps[i].target;
 return source;
}
/* MENU is a prefix, so ordinary game keys never wait for a chord window.
 * Only a button DOWN following MENU DOWN belongs to the system chord.
 * An already-held shoulder remains a game input. The raw evdev observer
 * dispatches the system action; this owner only isolates VT delivery. */
static bool gkd_system_consumed(unsigned int key)
{
 struct gkd_system_hotkey_lease *s = gkd_system_lease;
 return test_bit(key, gkd_system_blocked) || (s && s->configured &&
  (key == s->config.prefix || (key == s->config.button && s->button_blocked)));
}
static void gkd_system_sync(void)
{
 struct gkd_system_hotkey_lease *s = gkd_system_lease;
 unsigned int key;
 for_each_set_bit(key, gkd_system_blocked, KEY_CNT)
  if (!gkd_menu_physical[key]) __clear_bit(key, gkd_system_blocked);
 if (!s || !s->configured) return;
 s->prefix_down = gkd_menu_physical[s->config.prefix] != 0;
 s->button_down = gkd_menu_physical[s->config.button] != 0;
 /* Open/grab/drop recovery never turns an old hold into a fresh action. */
 s->used = s->prefix_down;
 s->button_blocked = s->prefix_down && s->button_down;
 s->primed = false;
}
static void gkd_input_reconcile(unsigned int key, bool repeat);
static void gkd_system_finish(struct gkd_system_hotkey_lease *s)
{
 if (!s->native_pulse) return;
 s->native_pulse = false;gkd_input_reconcile(s->native_target, false);
}
static bool gkd_system_filter(unsigned int key, int value)
{
 struct gkd_system_hotkey_lease *s = gkd_system_lease;
 bool down, consume;
 unsigned int target;
 if (test_bit(key, gkd_system_blocked)) {
  if (!gkd_menu_physical[key]) {
   __clear_bit(key, gkd_system_blocked);gkd_system_sync();
  }
  return true;
 }
 if (!s || !s->configured) return false;
 if (key == s->config.prefix) {
  down = gkd_menu_physical[key] != 0;
  if (down && !s->prefix_down) {
   s->prefix_down = true;s->used = false;s->primed = !s->button_down;
  } else if (!down && s->prefix_down) {
   s->prefix_down = false;
   if (!s->used) {
    /* Preserve native HOME for raw games; a configured game trigger is
     * consumed here and serviced by the authenticated MENU pulse instead. */
    target = gkd_input_destination(key);
    if (target && !s->native_pulse && !test_bit(target, key_down)) {
     /* Raw SDL consumers may poll key state rather than the event queue.
      * Preserve a bounded native press after deciding this was a single MENU. */
     s->native_target = target;s->native_pulse = true;
     kbd_keycode(target, 1, false);
     hrtimer_start(&s->timer, ms_to_ktime(80), HRTIMER_MODE_REL);
    }
   }
   s->used = false;s->primed = false;
  }
  return true;
 }
 if (key != s->config.button) return false;
 down = gkd_menu_physical[key] != 0;
 consume = s->button_blocked;
 if (down && !s->button_down) {
  s->button_down = true;
  if (s->prefix_down && s->primed) {
   s->used = true;s->button_blocked = true;consume = true;
  }
 } else if (!down && s->button_down) {
  s->button_down = false;s->button_blocked = false;
  if (s->prefix_down && !s->used) s->primed = true;
 }
 (void)value;return consume;
}
/* A repeating source contributes only its current ON phase. Independent
 * physical keys and other mapped sources still own the same destination. */
static bool gkd_input_source_held(unsigned int source)
{
 struct gkd_menu_vt_lease *lease = gkd_menu_lease;
 if (!gkd_menu_physical[source] || gkd_system_consumed(source)) return false;
 return !lease || !lease->configured || lease->repeat.source != source ||
        (lease->autofire_phase && !lease->autofire_paused);
}
static bool gkd_menu_held(unsigned int key)
{
 unsigned int i;
 if (gkd_input_destination(key) == key && gkd_input_source_held(key)) return true;
 if (!gkd_menu_lease || !gkd_menu_lease->configured) return false;
 for (i = 0; i < gkd_menu_lease->config.map_count; i++) {
  const struct gkd_input_route_map *map = &gkd_menu_lease->config.maps[i];
  if (map->source != key && map->target == key && gkd_input_source_held(map->source)) return true;
 }
 return false;
}
static void gkd_input_reconcile(unsigned int key, bool repeat)
{
 bool pulse = (gkd_menu_lease && gkd_menu_lease->pulse && gkd_menu_target(key)) ||
  (gkd_system_lease && gkd_system_lease->native_pulse && gkd_system_lease->native_target == key);
 bool down = gkd_menu_held(key) || pulse;
 if (down != test_bit(key, key_down)) kbd_keycode(key, down, false);
 else if (repeat && down && !pulse) kbd_keycode(key, 2, false);
}
/* Called under kbd_event_lock, including release/drop reconciliation. Timer
 * cancellation is deferred: a queued callback sees held=false and cannot rearm. */
static void gkd_autofire_sync(void)
{
 struct gkd_menu_vt_lease *lease = gkd_menu_lease;
 unsigned int source;
 bool down;
 if (!lease || !lease->configured || !(source = lease->repeat.source)) return;
 down = gkd_menu_physical[source] && !gkd_system_consumed(source) && !lease->autofire_paused;
 if (down == lease->autofire_held) return;
 lease->autofire_held = down;lease->autofire_phase = down;
 if (down) hrtimer_start(&lease->autofire_timer, ms_to_ktime(lease->repeat.on_ms), HRTIMER_MODE_REL);
 gkd_input_reconcile(gkd_input_destination(source), false);
}
static void gkd_menu_key(struct input_handle *handle, unsigned int key, int value)
{
 unsigned long *source = handle->private;
 bool old = test_bit(key, source);
 unsigned int target;
 if (value < 0 || value > 2) return;
 if (value != 2 && !!value != old) {
  __assign_bit(key, source, value);
  if (value) gkd_menu_physical[key]++;
  else gkd_menu_physical[key]--;
 }
 if (gkd_system_filter(key, value)) { gkd_autofire_sync();return; }
 gkd_autofire_sync();
 if (!gkd_menu_lease || !gkd_menu_lease->configured) {
  kbd_keycode(key, value, kbd_is_hw_raw(handle->dev));
  return;
 }
 target = gkd_input_destination(key);
 if (target) gkd_input_reconcile(target, value == 2 && old && key != gkd_menu_lease->repeat.source);
}
/* Refresh after input open/grab release under input->VT lock ordering. */
static void gkd_menu_sync(struct input_handle *handle)
{
 unsigned long flags;
 unsigned int key;
 unsigned long *source = handle->private;
 spin_lock_irqsave(&handle->dev->event_lock, flags);
 spin_lock(&kbd_event_lock);
 for (key = 0; key < KEY_CNT; key++) {
  bool down = test_bit(key, handle->dev->key);
  if (down != test_bit(key, source)) {
   __assign_bit(key, source, down);
   if (down) gkd_menu_physical[key]++;
   else gkd_menu_physical[key]--;
  }
 }
 gkd_system_sync();gkd_autofire_sync();
 if (gkd_menu_lease && gkd_menu_lease->configured)
  for (key = 1; key < KEY_CNT; key++) gkd_input_reconcile(key, false);
 spin_unlock(&kbd_event_lock);
 spin_unlock_irqrestore(&handle->dev->event_lock, flags);
}
static void gkd_menu_finish(struct gkd_menu_vt_lease *lease, int error)
{
 unsigned int i;
 if (!lease->pulse) return;
 lease->pulse = false;
 for (i = lease->config.count; i > 0; i--) {
  unsigned int key = lease->config.keys[i - 1];
  if (!gkd_menu_held(key)) kbd_keycode(key, 0, false);
 }
 lease->error = error;
 lease->pending = true;
 wake_up_interruptible(&lease->wait);
}
static enum hrtimer_restart gkd_menu_timeout(struct hrtimer *timer)
{
 struct gkd_menu_vt_lease *lease = container_of(timer, struct gkd_menu_vt_lease, timer);
 unsigned long flags;
 spin_lock_irqsave(&kbd_event_lock, flags);
 gkd_menu_finish(lease, 0);
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 gkd_menu_wake_vt();
 return HRTIMER_NORESTART;
}
static enum hrtimer_restart gkd_autofire_timeout(struct hrtimer *timer)
{
 struct gkd_menu_vt_lease *lease = container_of(timer, struct gkd_menu_vt_lease, autofire_timer);
 unsigned long flags;
 enum hrtimer_restart result = HRTIMER_NORESTART;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (gkd_menu_lease == lease && lease->configured && lease->repeat.source) {
  if (lease->autofire_held && !lease->autofire_paused &&
      gkd_menu_physical[lease->repeat.source] && !gkd_system_consumed(lease->repeat.source)) {
   /* A release/repress can rearm while an expired callback waits for
    * kbd_event_lock. Do not let that old callback shorten the new ON phase. */
   if (!ktime_before(ktime_get(), hrtimer_get_expires(timer))) {
    lease->autofire_phase = !lease->autofire_phase;
    hrtimer_forward_now(timer, ms_to_ktime(lease->autofire_phase ? lease->repeat.on_ms : lease->repeat.off_ms));
   }
   result = HRTIMER_RESTART;
  } else {
   lease->autofire_held = false;lease->autofire_phase = false;
  }
  gkd_input_reconcile(gkd_input_destination(lease->repeat.source), false);
 }
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 gkd_menu_wake_vt();
 return result;
}
static int gkd_menu_open(struct inode *inode, struct file *file)
{
 struct gkd_menu_vt_lease *lease;
 unsigned long flags;
 if (!capable(CAP_SYS_ADMIN)) return -EPERM;
 lease = kzalloc(sizeof(*lease), GFP_KERNEL);
 if (!lease) return -ENOMEM;
 init_waitqueue_head(&lease->wait);
 mutex_init(&lease->operation);
 hrtimer_init(&lease->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 lease->timer.function = gkd_menu_timeout;
 hrtimer_init(&lease->autofire_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 lease->autofire_timer.function = gkd_autofire_timeout;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (gkd_menu_lease) {
  spin_unlock_irqrestore(&kbd_event_lock, flags);
  kfree(lease);return -EBUSY;
 }
 gkd_menu_lease = lease;
 file->private_data = lease;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 return nonseekable_open(inode, file);
}
static long gkd_menu_ioctl_locked(struct file *file, unsigned int command, unsigned long arg)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 struct gkd_menu_vt_config config;
 struct gkd_input_autofire_config repeat = {0};
 unsigned long flags;
 unsigned int i, j;
 int error = 0;
 if (command == GKD_MENU_VT_CONFIG || command == GKD_INPUT_ROUTE_UPDATE || command == GKD_INPUT_ROUTE_REPEAT) {
  if (command == GKD_INPUT_ROUTE_REPEAT) {
   struct gkd_input_route_repeat_config request;
   if (copy_from_user(&request, (void __user *)arg, sizeof(request))) return -EFAULT;
   config = request.route;repeat = request.repeat;
  } else if (copy_from_user(&config, (void __user *)arg, sizeof(config))) return -EFAULT;
  if (repeat.reserved || repeat.source > KEY_MAX ||
      (!repeat.source && (repeat.on_ms || repeat.off_ms)) ||
      (repeat.source && (repeat.source == config.trigger || repeat.on_ms < 20 ||
       repeat.on_ms > 500 || repeat.off_ms < 20 || repeat.off_ms > 500))) return -EINVAL;
  if (config.version != GKD_MENU_VT_VERSION || config.reserved ||
      config.count > GKD_MENU_VT_KEYS || config.hold_ms < 20 || config.hold_ms > 500 ||
      !config.trigger || config.trigger > KEY_MAX || config.map_count > GKD_INPUT_ROUTE_MAPS)
   return -EINVAL;
  for (i = 0; i < GKD_MENU_VT_KEYS; i++) {
   if (i >= config.count) { if (config.keys[i]) return -EINVAL;continue; }
   if (!config.keys[i] || config.keys[i] > KEY_MAX) return -EINVAL;
   for (j = 0; j < i; j++) if (config.keys[i] == config.keys[j]) return -EINVAL;
  }
  for (i = 0; i < GKD_INPUT_ROUTE_MAPS; i++) {
   if (i >= config.map_count) {
    if (config.maps[i].source || config.maps[i].target) return -EINVAL;
    continue;
   }
   if (!config.maps[i].source || config.maps[i].source > KEY_MAX ||
       config.maps[i].source == config.trigger || config.maps[i].target > KEY_MAX) return -EINVAL;
   for (j = 0; j < i; j++) if (config.maps[i].source == config.maps[j].source) return -EINVAL;
  }
  if (repeat.source) {
   unsigned int target = repeat.source;
   for (i = 0; i < config.map_count; i++)
    if (config.maps[i].source == repeat.source) target = config.maps[i].target;
   if (!target) return -EINVAL;
  }
 }
 spin_lock_irqsave(&kbd_event_lock, flags);
 switch (command) {
 case GKD_MENU_VT_CONFIG:
  if (lease->configured) { error = -EALREADY;break; }
  /* Scheme changes never migrate a held key into a new action. */
  for (i = 1; i < KEY_CNT; i++)
   if (gkd_menu_physical[i] || test_bit(i, key_down)) { error = -EBUSY;break; }
  if (error) break;
  lease->config = config;lease->configured = true;break;
 case GKD_INPUT_ROUTE_UPDATE:
 case GKD_INPUT_ROUTE_REPEAT:
  if (!lease->configured) { error = -ENXIO;break; }
  /* This operation cannot change MENU identity, pulses or ownership. */
  if (config.trigger != lease->config.trigger || config.count != lease->config.count ||
      config.hold_ms != lease->config.hold_ms ||
      memcmp(config.keys, lease->config.keys, sizeof(config.keys))) { error = -EPERM;break; }
  /* Losing orientation stops repetition immediately, even if restoring the
   * base maps must wait for physical releases. Never synthesize a held Y here. */
  if (lease->repeat.source && !repeat.source) {
   lease->autofire_paused = true;gkd_autofire_sync();
  }
  if (!memcmp(&config, &lease->config, sizeof(config)) &&
      !memcmp(&repeat, &lease->repeat, sizeof(repeat)) && !lease->autofire_paused) break;
  if (lease->pulse || lease->pending) { error = -EBUSY;break; }
  /* Never turn an old physical hold into a new action or lose its release. */
  for (i = 1; i < KEY_CNT; i++)
   if (gkd_menu_physical[i] || test_bit(i, key_down)) { error = -EBUSY;break; }
  if (!error) {
   lease->config = config;lease->repeat = repeat;
   lease->autofire_held = false;lease->autofire_phase = false;lease->autofire_paused = false;
  }
  break;
 case GKD_MENU_VT_PULSE:
  if (!lease->configured || !lease->config.count) { error = -EOPNOTSUPP;break; }
  if (lease->pulse || lease->pending) { error = -EBUSY;break; }
  for (i = 0; i < lease->config.count; i++)
   if (gkd_menu_held(lease->config.keys[i])) error = -EBUSY;
  if (error) break;
  lease->pulse = true;
  for (i = 0; i < lease->config.count; i++) kbd_keycode(lease->config.keys[i], 1, false);
  hrtimer_start(&lease->timer, ms_to_ktime(lease->config.hold_ms), HRTIMER_MODE_REL);
  break;
 case GKD_MENU_VT_CANCEL:
  gkd_menu_finish(lease, ECANCELED);break;
 default:error = -ENOTTY;
 }
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 if (!error && command == GKD_MENU_VT_CANCEL) hrtimer_cancel(&lease->timer);
 if (!error) gkd_menu_wake_vt();
 return error;
}
static long gkd_menu_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 long result;
 mutex_lock(&lease->operation);
 result = gkd_menu_ioctl_locked(file, command, arg);
 mutex_unlock(&lease->operation);
 return result;
}
static __poll_t gkd_menu_poll(struct file *file, poll_table *wait)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 poll_wait(file, &lease->wait, wait);
 return READ_ONCE(lease->pending) ? EPOLLIN | EPOLLRDNORM : 0;
}
static ssize_t gkd_menu_read_locked(struct file *file, char __user *buffer, size_t size, loff_t *offset)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 struct gkd_menu_vt_receipt result;
 unsigned long flags;
 if (size != sizeof(result)) return -EINVAL;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (!lease->pending) { spin_unlock_irqrestore(&kbd_event_lock, flags);return -EAGAIN; }
 result.version = GKD_MENU_VT_VERSION;result.error = lease->error;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 if (copy_to_user(buffer, &result, sizeof(result))) return -EFAULT;
 spin_lock_irqsave(&kbd_event_lock, flags);
 lease->pending = false;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 return sizeof(result);
}
static ssize_t gkd_menu_read(struct file *file, char __user *buffer, size_t size, loff_t *offset)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 ssize_t result;
 mutex_lock(&lease->operation);
 result = gkd_menu_read_locked(file, buffer, size, offset);
 mutex_unlock(&lease->operation);
 return result;
}
static int gkd_menu_release(struct inode *inode, struct file *file)
{
 struct gkd_menu_vt_lease *lease = file->private_data;
 unsigned long flags;
 /* Quiesce input before cancel_sync: no key/timer path may rearm a timer
  * whose containing lease is about to be freed. */
 spin_lock_irqsave(&kbd_event_lock, flags);
 lease->autofire_paused = true;lease->autofire_held = false;lease->autofire_phase = false;
 lease->repeat.source = 0;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 hrtimer_cancel(&lease->autofire_timer);
 hrtimer_cancel(&lease->timer);
 spin_lock_irqsave(&kbd_event_lock, flags);
 gkd_menu_finish(lease, ECANCELED);
 if (lease->configured) {
  unsigned int key;
  /* Release route-owned outputs before handing the VT back to the frontend.
   * Do not synthesize fresh raw presses for keys still physically held. */
  for (key = 1; key < KEY_CNT; key++)
   if (test_bit(key, key_down)) kbd_keycode(key, 0, false);
 }
 gkd_menu_lease = NULL;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 gkd_menu_wake_vt();kfree(lease);return 0;
}
static void gkd_menu_disconnect(struct input_handle *handle)
{
 unsigned int key;
 unsigned long flags;
 spin_lock_irqsave(&kbd_event_lock, flags);
 for_each_set_bit(key, (unsigned long *)handle->private, KEY_CNT)
  gkd_menu_physical[key]--;
 gkd_system_sync();gkd_autofire_sync();
 if (gkd_menu_lease && gkd_menu_lease->configured)
  for (key = 1; key < KEY_CNT; key++) gkd_input_reconcile(key, false);
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 kfree(handle->private);
}
static const struct file_operations gkd_menu_fops = {
 .owner = THIS_MODULE, .open = gkd_menu_open, .release = gkd_menu_release,
 .unlocked_ioctl = gkd_menu_ioctl, .compat_ioctl = compat_ptr_ioctl,
 .poll = gkd_menu_poll, .read = gkd_menu_read, .llseek = no_llseek,
};
static struct miscdevice gkd_menu_device = {
 .minor = MISC_DYNAMIC_MINOR, .name = "gkd-input-route", .fops = &gkd_menu_fops, .mode = 0600,
};
static enum hrtimer_restart gkd_system_timeout(struct hrtimer *timer)
{
 struct gkd_system_hotkey_lease *s = container_of(timer, struct gkd_system_hotkey_lease, timer);
 unsigned long flags;
 spin_lock_irqsave(&kbd_event_lock, flags);gkd_system_finish(s);
 spin_unlock_irqrestore(&kbd_event_lock, flags);gkd_menu_wake_vt();
 return HRTIMER_NORESTART;
}
static int gkd_system_open(struct inode *inode, struct file *file)
{
 struct gkd_system_hotkey_lease *s;
 unsigned long flags;
 if (!capable(CAP_SYS_ADMIN)) return -EPERM;
 s = kzalloc(sizeof(*s), GFP_KERNEL);if (!s) return -ENOMEM;
 hrtimer_init(&s->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 s->timer.function = gkd_system_timeout;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (gkd_system_lease) {
  spin_unlock_irqrestore(&kbd_event_lock, flags);kfree(s);return -EBUSY;
 }
 gkd_system_lease = s;file->private_data = s;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 return nonseekable_open(inode, file);
}
static long gkd_system_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
 struct gkd_system_hotkey_lease *s = file->private_data;
 struct gkd_system_hotkey_config config;
 unsigned long flags;
 int error = 0;
 if (command != GKD_SYSTEM_HOTKEY_CONFIG) return -ENOTTY;
 if (copy_from_user(&config, (void __user *)arg, sizeof(config))) return -EFAULT;
 if (config.version != GKD_SYSTEM_HOTKEY_VERSION || config.reserved ||
     !config.prefix || config.prefix > KEY_MAX || !config.button ||
     config.button > KEY_MAX || config.prefix == config.button) return -EINVAL;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (s->configured) error = -EALREADY;
 else {
  s->config = config;s->configured = true;gkd_system_sync();
  /* A pre-existing native hold cannot remain down after being reserved. */
  gkd_input_reconcile(config.prefix, false);
  if (s->button_blocked) {
   unsigned int target = gkd_input_destination(config.button);
   if (target) gkd_input_reconcile(target, false);
  }
 }
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 return error;
}
static int gkd_system_release(struct inode *inode, struct file *file)
{
 struct gkd_system_hotkey_lease *s = file->private_data;
 unsigned long flags;
 spin_lock_irqsave(&kbd_event_lock, flags);
 if (s->configured) {
  if (gkd_menu_physical[s->config.prefix])
   __set_bit(s->config.prefix, gkd_system_blocked);
  if (s->button_blocked && gkd_menu_physical[s->config.button])
   __set_bit(s->config.button, gkd_system_blocked);
 }
 gkd_system_finish(s);
 /* Disable scheduling under the input lock BEFORE cancel: otherwise a
  * racing MENU release could re-arm the timer between cancel and free. */
 s->configured = false;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 hrtimer_cancel(&s->timer);
 spin_lock_irqsave(&kbd_event_lock, flags);
 gkd_system_lease = NULL;
 spin_unlock_irqrestore(&kbd_event_lock, flags);
 gkd_menu_wake_vt();kfree(s);return 0;
}
static const struct file_operations gkd_system_fops = {
 .owner = THIS_MODULE, .open = gkd_system_open, .release = gkd_system_release,
 .unlocked_ioctl = gkd_system_ioctl, .compat_ioctl = compat_ptr_ioctl, .llseek = no_llseek,
};
static struct miscdevice gkd_system_device = {
 .minor = MISC_DYNAMIC_MINOR, .name = "gkd-system-hotkey", .fops = &gkd_system_fops, .mode = 0600,
};
static int __init gkd_menu_device_init(void)
{
 int error = misc_register(&gkd_menu_device);
 if (error) return error;
 error = misc_register(&gkd_system_device);
 if (error) misc_deregister(&gkd_menu_device);
 return error;
}
device_initcall(gkd_menu_device_init);
