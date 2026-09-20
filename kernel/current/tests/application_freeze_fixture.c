/* SPDX-License-Identifier: GPL-2.0 */
/* Kernel services are stubbed. DRIVER_FRAGMENT is extracted byte-for-byte
 * from the final candidate driver; the lease algorithm is never duplicated. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gkd-ui-plane.h"

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uint64_t dma_addr_t;

#define X1830_FB_XRES 320U
#define X1830_FB_YRES 240U
#define X1830_FB_LINE_LENGTH (X1830_FB_XRES * 2U)
#define X1830_FB_FRAME_SIZE (X1830_FB_XRES * X1830_FB_YRES * 2U)
#define X1830_FB_SIZE (2U * X1830_FB_FRAME_SIZE)
#define X1830_COMPOSITE_REFRESH_MS 20U
#define X1830_DMA_FAULT_RECHECK_MS 100U
#define CONFIG_FB_X1830_USER_PLANE 1
#define IS_ENABLED(option) 1
#define CAP_SYS_ADMIN 21
#define __user
#define WRITE_ONCE(x, value) ((x) = (value))
#define lower_32_bits(value) ((u32)(value))
#define upper_32_bits(value) ((u32)((uint64_t)(value) >> 32))
#define msecs_to_jiffies(value) ((unsigned long)(((value) + 9U) / 10U))
#define time_before(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)
#define dma_wmb() ((void)0)
#define memzero_explicit(pointer, bytes) memset((pointer), 0, (bytes))
#define system_highpri_wq NULL
#define container_of(pointer, type, member) \
    ((type *)((char *)(pointer) - offsetof(type, member)))
#define to_delayed_work(pointer) ((struct delayed_work *)(pointer))

enum {
    X1830_MENU_HIDDEN,
    X1830_MENU_EJECT_CONFIRM,
    X1830_MENU_CHARGE,
    X1830_MENU_STORAGE_DISABLED,
    X1830_MENU_STORAGE,
    X1830_MENU_DEBUG,
    X1830_MENU_CHOOSER,
    X1830_MENU_LOADING,
};

struct pid { int refs; bool exited; };
struct task_struct { struct pid *tgid; };
struct mutex { bool held; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct fb_var_screeninfo { u32 xoffset; u32 yoffset; };
struct fb_info { void *par; };
struct desc_block { struct { u32 buffer_addr; } layer[1]; };

struct x1830_fb {
    struct { bool paused; int fault; } dma;
    struct desc_block *descs;
    struct mutex volume_osd_lock;
    bool volume_osd_removing;
    struct delayed_work composite_work;
    bool composite_active;
    u32 composite_source_addr;
    unsigned long volume_osd_expires;
    unsigned long power_osd_expires;
    unsigned long menu_expires;
    unsigned menu_view;
    dma_addr_t composite_dma;
    void *composite_virt;
    dma_addr_t fb_dma;
    void *fb_virt;
    u32 latest_scanout_addr;
    bool usb_debug_freeze;
    void *usb_debug_frozen_virt;
    bool usb_debug_frozen_valid;
    bool usb_debug_loading_active;
    bool usb_debug_loading_held;
    unsigned long usb_debug_loading_hard_expires;
    u8 usb_debug_loading_target;
    struct pid *user_debug_freeze_owner;
    u32 user_debug_freeze_sequence;
    unsigned long user_debug_freeze_expires;
};

static struct task_struct task;
static struct task_struct *current = &task;
static unsigned long jiffies;
static int cap = 1;
static int copy_fault;
static unsigned schedules;
static unsigned captures;
static unsigned reasserts;
static unsigned cases;
static u16 *visible_output;

static int capable(int capability) { return cap && capability == CAP_SYS_ADMIN; }
static unsigned long copy_from_user(void *to, const void *from, size_t bytes)
{
    if (copy_fault) return bytes;
    memcpy(to, from, bytes);
    return 0;
}
static struct pid *task_tgid(struct task_struct *source) { return source->tgid; }
static struct pid *get_pid(struct pid *pid) { if (pid) ++pid->refs; return pid; }
static void put_pid(struct pid *pid) { if (pid) { assert(pid->refs > 0); --pid->refs; } }
static bool thread_group_exited(struct pid *pid) { return pid->exited; }
static void mutex_lock(struct mutex *lock) { assert(!lock->held); lock->held = true; }
static void mutex_unlock(struct mutex *lock) { assert(lock->held); lock->held = false; }
static void queue_delayed_work(void *queue, struct delayed_work *work, unsigned long delay)
{ (void)queue; (void)work; assert(delay == msecs_to_jiffies(20U) || delay == msecs_to_jiffies(100U)); ++schedules; }
static void mod_delayed_work(void *queue, struct delayed_work *work, unsigned long delay)
{ queue_delayed_work(queue, work, delay); }

static bool x1830_user_menu_live_locked(struct x1830_fb *fb) { (void)fb; return false; }
static bool x1830_user_plane_live_locked(struct x1830_fb *fb) { (void)fb; return false; }
static int x1830_software_composite_locked(struct x1830_fb *fb, u32 source, u32 *address)
{
    (void)source;
    assert(fb->usb_debug_frozen_valid);
    memcpy(visible_output, fb->usb_debug_frozen_virt, X1830_FB_FRAME_SIZE);
    *address = lower_32_bits(fb->composite_dma);
    return 0;
}
static int x1830_hardware_osd_reassert_locked(struct x1830_fb *fb, u32 address)
{
    assert(fb->volume_osd_lock.held);
    fb->composite_source_addr = address;
    fb->composite_active = fb->usb_debug_frozen_valid;
    fb->descs->layer[0].buffer_addr = fb->composite_active ?
        lower_32_bits(fb->composite_dma) : address;
    if (fb->composite_active)
        mod_delayed_work(system_highpri_wq, &fb->composite_work,
                         msecs_to_jiffies(X1830_COMPOSITE_REFRESH_MS));
    ++reasserts;
    return fb->dma.paused ? -EAGAIN : fb->dma.fault;
}
#define x1830_dma_ready(copy) ((copy)->paused ? -EAGAIN : (copy)->fault)

static int gkd_ui_menu_get_caps(unsigned long arg) { (void)arg; return -ENOTTY; }
static int gkd_ui_menu_submit_ioctl(struct x1830_fb *fb, unsigned long arg)
{ (void)fb; (void)arg; return -ENOTTY; }
static int gkd_ui_menu_clear_ioctl(struct x1830_fb *fb) { (void)fb; return -ENOTTY; }
static int gkd_ui_menu_hide_ioctl(struct x1830_fb *fb) { (void)fb; return -ENOTTY; }
static int gkd_ui_plane_get_caps(unsigned long arg) { (void)arg; return -ENOTTY; }
static int gkd_ui_plane_submit_ioctl(struct x1830_fb *fb, unsigned long arg)
{ (void)fb; (void)arg; return -ENOTTY; }
static int gkd_ui_plane_clear_ioctl(struct x1830_fb *fb) { (void)fb; return -ENOTTY; }

/* DRIVER_FRAGMENT */

static struct x1830_fb fb;
static struct fb_info info = { &fb };
static struct desc_block descs;
static struct pid owner;
static struct pid competitor;
static struct gkd_ui_freeze_submit packet;
static u16 pages[2U * X1830_FB_XRES * X1830_FB_YRES];
static u16 frozen[X1830_FB_XRES * X1830_FB_YRES];
static u16 output[X1830_FB_XRES * X1830_FB_YRES];

static int submit(void)
{
    return x1830_fb_ioctl(&info, GKD_UI_FREEZE_SUBMIT, (unsigned long)&packet);
}

static int clear(void)
{
    return x1830_fb_ioctl(&info, GKD_UI_FREEZE_CLEAR, 0);
}

static void expect_submit(int error)
{
    struct x1830_fb before = fb;
    int owner_refs = owner.refs;
    int competitor_refs = competitor.refs;
    u16 before_frozen[X1830_FB_XRES * X1830_FB_YRES];
    memcpy(before_frozen, frozen, sizeof(frozen));
    assert(submit() == error);
    assert(!memcmp(&before, &fb, sizeof(fb)));
    assert(!memcmp(before_frozen, frozen, sizeof(frozen)));
    assert(owner.refs == owner_refs && competitor.refs == competitor_refs);
    ++cases;
}

static void fill_page(unsigned page, u16 seed)
{
    for (unsigned i = 0; i < X1830_FB_XRES * X1830_FB_YRES; ++i)
        pages[page * X1830_FB_XRES * X1830_FB_YRES + i] = (u16)(seed ^ i);
}

int main(void)
{
    memset(&fb, 0, sizeof(fb));
    memset(&descs, 0, sizeof(descs));
    fill_page(0, 0x1357U);
    fill_page(1, 0x2468U);
    visible_output = output;
    fb.descs = &descs;
    fb.fb_dma = 0x2000U;
    fb.composite_dma = 0x100000U;
    fb.fb_virt = pages;
    fb.composite_virt = output;
    fb.usb_debug_frozen_virt = frozen;
    fb.latest_scanout_addr = lower_32_bits(fb.fb_dma);
    fb.composite_source_addr = fb.latest_scanout_addr;
    task.tgid = &owner;
    jiffies = 100U;
    packet = (struct gkd_ui_freeze_submit) { .ttl_ms = 100U, .sequence = 1U };

    assert(sizeof(packet) == 16U);
    assert(_IOC_NR(GKD_UI_FREEZE_SUBMIT) == 0x77U);
    assert(_IOC_SIZE(GKD_UI_FREEZE_SUBMIT) == 16U);
    assert(_IOC_NR(GKD_UI_FREEZE_CLEAR) == 0x78U);
    ++cases;

    cap = 0; expect_submit(-EPERM); assert(clear() == -EPERM); cap = 1;
    copy_fault = 1; expect_submit(-EFAULT); copy_fault = 0;
    packet.ttl_ms = 19U; expect_submit(-EINVAL);
    packet.ttl_ms = 10001U; expect_submit(-EINVAL);
    packet.ttl_ms = 100U; packet.sequence = 0U; expect_submit(-EINVAL);
    packet.sequence = 1U; packet.reserved[0] = 1U; expect_submit(-EINVAL);
    packet.reserved[0] = 0U; packet.reserved[1] = 1U; expect_submit(-EINVAL);
    packet.reserved[1] = 0U;

    fb.dma.paused = true; expect_submit(-EAGAIN); fb.dma.paused = false;
    fb.dma.fault = -EIO; expect_submit(-EIO); fb.dma.fault = 0;

    assert(!submit()); ++captures;
    assert(owner.refs == 1 && fb.user_debug_freeze_owner == &owner);
    assert(fb.user_debug_freeze_sequence == 1U && fb.user_debug_freeze_expires == 110U);
    assert(fb.usb_debug_frozen_valid && !fb.usb_debug_freeze);
    assert(!memcmp(frozen, pages, X1830_FB_FRAME_SIZE));
    assert(descs.layer[0].buffer_addr == lower_32_bits(fb.composite_dma));
    ++cases;

    u16 original[X1830_FB_XRES * X1830_FB_YRES];
    memcpy(original, frozen, sizeof(original));
    fill_page(0, 0x7777U);
    packet.sequence = 2U; packet.ttl_ms = 200U; jiffies = 102U;
    assert(!submit());
    assert(owner.refs == 1 && fb.user_debug_freeze_expires == 122U);
    assert(!memcmp(original, frozen, sizeof(original)));
    ++cases;
    expect_submit(-ESTALE);
    packet.sequence = 1U; expect_submit(-ESTALE);

    task.tgid = &competitor; packet.sequence = 3U;
    expect_submit(-EBUSY); assert(clear() == -EBUSY); ++cases;
    task.tgid = &owner;
    assert(!clear());
    assert(!owner.refs && !fb.user_debug_freeze_owner && !fb.usb_debug_frozen_valid);
    for (unsigned i = 0; i < X1830_FB_XRES * X1830_FB_YRES; ++i) assert(!frozen[i]);
    assert(descs.layer[0].buffer_addr == fb.latest_scanout_addr);
    assert(!clear());
    ++cases;

    packet.sequence = 1U; packet.ttl_ms = 100U; jiffies = 200U;
    assert(!submit()); ++captures;
    assert(!memcmp(frozen, pages, X1830_FB_FRAME_SIZE));
    struct fb_var_screeninfo var = { .yoffset = X1830_FB_YRES };
    assert(!x1830_fb_pan_display(&var, &info));
    assert(!fb.usb_debug_freeze);
    assert(fb.latest_scanout_addr == lower_32_bits(fb.fb_dma + X1830_FB_FRAME_SIZE));
    assert(fb.composite_source_addr == fb.latest_scanout_addr);
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!memcmp(output, frozen, X1830_FB_FRAME_SIZE));
    assert(descs.layer[0].buffer_addr == lower_32_bits(fb.composite_dma));
    ++cases;

    jiffies = fb.user_debug_freeze_expires;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!owner.refs && !fb.user_debug_freeze_owner && !fb.usb_debug_frozen_valid);
    assert(!fb.composite_active);
    assert(descs.layer[0].buffer_addr == lower_32_bits(fb.fb_dma + X1830_FB_FRAME_SIZE));
    ++cases;

    task.tgid = &competitor; competitor.exited = false;
    packet.sequence = 1U; packet.ttl_ms = 1000U;
    assert(!submit()); ++captures;
    assert(!memcmp(frozen, pages + X1830_FB_XRES * X1830_FB_YRES,
                   X1830_FB_FRAME_SIZE));
    assert(!owner.refs && competitor.refs == 1);
    ++cases;

    competitor.exited = true;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!competitor.refs && !fb.user_debug_freeze_owner);
    assert(!fb.usb_debug_frozen_valid && !fb.composite_active);
    assert(descs.layer[0].buffer_addr == fb.composite_source_addr);
    ++cases;

    competitor.exited = false; task.tgid = &owner;
    packet.sequence = UINT32_MAX; packet.ttl_ms = 20U; jiffies = UINT32_MAX - 1U;
    assert(!submit()); ++captures;
    packet.sequence = 1U; expect_submit(-ESTALE);
    task.tgid = &competitor; expect_submit(-EBUSY);
    jiffies = (uint32_t)fb.user_debug_freeze_expires;
    assert(!submit()); ++captures;
    assert(!owner.refs && competitor.refs == 1 && fb.user_debug_freeze_sequence == 1U);
    assert(!clear() && !competitor.refs);
    ++cases;

    fb.volume_osd_removing = true;
    task.tgid = &owner; packet.sequence = 1U; expect_submit(-ENODEV);
    assert(clear() == -ENODEV);
    fb.volume_osd_removing = false;
    assert(x1830_fb_ioctl(&info, 0xffffffffU, 0) == -ENOTTY);
    assert(captures == 5U);
    printf("GKD_APP_FREEZE_ACTUAL=PASS cases=%u captures=%u schedules=%u reasserts=%u owner/renew/replay/foreign/clear/pan/expiry/death/wrap/restoration/no-legacy/no-allocation\n",
           cases, captures, schedules, reasserts);
    return 0;
}
