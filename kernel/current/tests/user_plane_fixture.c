/* SPDX-License-Identifier: GPL-2.0 */
/* Kernel services are stubbed; DRIVER_FRAGMENT is extracted unchanged from
 * the real pinned source after the final checked A patch stack. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "gkd-ui-plane.h"
#include "gkd-ui-pixels.h"
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define X1830_FB_FRAME_SIZE (320U*240U*2U)
#define X1830_FB_SIZE (2U*X1830_FB_FRAME_SIZE)
#define IS_ENABLED(x) 1
#define lower_32_bits(n) ((u32)(n))
#define WARN_ON_ONCE(x) (x)
#define X1830_VOLUME_OSD_X 0U
#define X1830_VOLUME_OSD_Y 0U
#define X1830_VOLUME_OSD_WIDTH 0U
#define X1830_VOLUME_OSD_HEIGHT 0U
enum {X1830_MENU_HIDDEN, X1830_MENU_EJECT_CONFIRM, X1830_MENU_CHARGE,
      X1830_MENU_STORAGE_DISABLED, X1830_MENU_STORAGE, X1830_MENU_DEBUG, X1830_MENU_CHOOSER, X1830_MENU_LOADING};
#define X1830_POWER_HIDDEN 0U
#define __user
#define CAP_SYS_ADMIN 21
#define X1830_FB_XRES 320U
#define IS_ERR(p) ((uintptr_t)(p) > (uintptr_t)-4096)
#define PTR_ERR(p) ((long)(p))
#define ERR_PTR(e) ((void *)(long)(e))
#define upper_32_bits(n) ((u32)((uint64_t)(n) >> 32))
#define u64_to_user_ptr(n) ((void *)(uintptr_t)(n))
/* Model MIPS HZ=100 tick arithmetic, including 32-bit wrap, without replacing
 * any driver state transition or validation code. */
#define msecs_to_jiffies(n) ((unsigned long)(((n) + 9U) / 10U))
#define jiffies_to_msecs(n) ((unsigned)((uint32_t)(n) * 10U))
#define time_before(a,b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)
#define time_after_eq(a,b) (!time_before(a,b))
#define time_after(a,b) time_before(b,a)
#define WRITE_ONCE(x,v) ((x)=(v))
#define X1830_COMPOSITE_REFRESH_MS 20U
#define X1830_DMA_FAULT_RECHECK_MS 100U
#define dma_wmb() ((void)0)
#define system_highpri_wq NULL
#define system_wq NULL
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
#define to_delayed_work(ptr) ((struct delayed_work *)(ptr))
#define memzero_explicit(p,n) memset(p,0,n)
#define dev_warn(...) (++warnings)
struct pid { int refs; };
struct task_struct { struct pid *tgid; };
struct mutex { int locked; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct desc_block { struct { u32 buffer_addr; } layer[1]; };
struct x1830_dma_compositor {
    void *source, *output, *stage;
    u32 output_dma;
    size_t frame_bytes;
    bool paused;
    int fault;
};
struct x1830_fb {
    struct desc_block *descs;
    u32 composite_source_addr;
    u16 *user_menu_virt;
    struct pid *user_menu_owner;
    u32 user_menu_sequence;
    unsigned long user_menu_expires, user_menu_started;
    u32 user_menu_transition_ms, user_menu_animation_ms, user_menu_from_rows;
    bool user_menu_closing;
    struct pid *user_plane_owner;
    u32 user_plane_sequence;
    unsigned long user_plane_started, user_plane_expires;
    u32 user_plane_ttl_ms, user_plane_fade_ms;
    bool user_plane_active, user_plane_fade_done, volume_osd_removing, composite_active;
    u32 *user_plane_virt;
    struct mutex volume_osd_lock;
    struct delayed_work composite_work;
    u32 latest_scanout_addr;
    uint64_t fb_dma, composite_dma;
    void *fb_virt, *composite_virt, *usb_debug_frozen_virt;
    u32 *menu_osd_virt;
    unsigned long volume_osd_expires, menu_expires, power_osd_expires;
    struct x1830_dma_compositor dma;
    unsigned menu_view;
    u8 composite_front;
    bool usb_debug_frozen_valid;
    bool usb_debug_loading_active, usb_debug_loading_held;
    struct pid *user_debug_freeze_owner;
    u32 user_debug_freeze_sequence;
    unsigned long user_debug_freeze_expires;
    unsigned long usb_debug_loading_hard_expires;
    unsigned usb_debug_loading_target, menu_selection, power_osd_state;
    struct delayed_work volume_osd_clear_work;
};
struct fb_info { void *par; };
static struct task_struct task, *current = &task;
static unsigned long test_ticks;
static unsigned clock_reads, advance_on_read;
static unsigned long advance_to;
static unsigned long *test_jiffies(void)
{
    if (advance_on_read && ++clock_reads == advance_on_read) test_ticks=advance_to;
    return &test_ticks;
}
#define jiffies (*test_jiffies())
static int cap = 1, header_fault, payload_error, output_fault, mutate_after_copy;
static unsigned dma_prepare_calls, dma_cached_calls, dma_finish_calls, dma_direct_calls, dma_stage_cpu_calls;
static unsigned captures, allocations, reasserts, schedules, unfreezes, warnings;
static unsigned long last_delay;
static u32 payload[GKD_UI_PLANE_PIXELS];
static u16 menu_payload[GKD_UI_MENU_PIXELS];
static int capable(int c) { return c == CAP_SYS_ADMIN && cap; }
static unsigned long copy_from_user(void *d, const void *s, size_t n)
{
    size_t copied = header_fault ? n / 2U : n;
    memcpy(d, s, copied); return n - copied;
}
static unsigned long copy_to_user(void *d, const void *s, size_t n)
{
    if (output_fault) return n;
    memcpy(d, s, n); return 0;
}
static void *memdup_user(const void *s, size_t n)
{
    void *p;
    ++captures;
    if (payload_error) return ERR_PTR(-payload_error);
    if ((uintptr_t)s != 0x1000U) return ERR_PTR(-EFAULT);
    p = malloc(n); assert(p); ++allocations; memcpy(p, payload, n);
    if (mutate_after_copy) memset(payload, 0xee, sizeof(payload));
    return p;
}
static void kfree(void *p) { assert(p && allocations); --allocations; free(p); }
static void kvfree(void *p) { if (p) kfree(p); }
static void *vmemdup_user(const void *s, size_t n)
{
    void *p;
    ++captures;
    if (payload_error) return ERR_PTR(-payload_error);
    if ((uintptr_t)s != 0x2000U) return ERR_PTR(-EFAULT);
    assert(n == sizeof(menu_payload));
    p = malloc(n); assert(p); ++allocations; memcpy(p, menu_payload, n);
    if (mutate_after_copy) memset(menu_payload, 0xee, sizeof(menu_payload));
    return p;
}
static struct pid *task_tgid(struct task_struct *t) { return t->tgid; }
static struct pid *get_pid(struct pid *p) { if (p) ++p->refs; return p; }
static void put_pid(struct pid *p) { if (p) { assert(p->refs > 0); --p->refs; } }
static void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = 0; }
static void mod_delayed_work(void *q, struct delayed_work *w, unsigned long delay)
{ (void)q; (void)w; last_delay=delay; ++schedules; }
static void queue_delayed_work(void *q, struct delayed_work *w, unsigned long delay)
{ mod_delayed_work(q,w,delay); }
static int x1830_hardware_osd_reassert_locked(struct x1830_fb *f, u32 address)
{ (void)address; assert(f->volume_osd_lock.locked); ++reasserts; return 0; }
static bool thread_group_exited(struct pid *p) { (void)p; return false; }
static int dma_direct_error, dma_finish_error;
static void x1830_dma_stage_cpu(struct x1830_dma_compositor *d)
{ (void)d; ++dma_stage_cpu_calls; }
static int x1830_dma_copy_output(struct x1830_dma_compositor *d, size_t source_offset, size_t output_offset)
{ if (d->paused) return -EAGAIN; if (d->fault) return d->fault; ++dma_direct_calls;
  if (dma_direct_error) { d->fault=dma_direct_error; return d->fault; }
  memcpy((uint8_t *)d->output+output_offset,(uint8_t *)d->source+source_offset,d->frame_bytes); return 0; }
static int x1830_dma_prepare(struct x1830_dma_compositor *d, size_t offset)
{ if (d->paused) return -EAGAIN; if (d->fault) return d->fault; ++dma_prepare_calls; memcpy(d->stage,(uint8_t *)d->source+offset,d->frame_bytes); x1830_dma_stage_cpu(d); return 0; }
static int x1830_dma_prepare_cached(struct x1830_dma_compositor *d, const void *source)
{ if (d->paused) return -EAGAIN; if (d->fault) return d->fault; ++dma_cached_calls; memcpy(d->stage,source,d->frame_bytes); return 0; }
static int x1830_dma_finish(struct x1830_dma_compositor *d, size_t offset, u32 *address)
{ if (d->paused) return -EAGAIN; if (d->fault) return d->fault; ++dma_finish_calls; if (dma_finish_error) { d->fault=dma_finish_error; return d->fault; } memcpy((uint8_t *)d->output+offset,d->stage,d->frame_bytes); x1830_dma_stage_cpu(d); *address=d->output_dma+(u32)offset; return 0; }
static int x1830_dma_ready(struct x1830_dma_compositor *d)
{ return d->paused ? -EAGAIN : d->fault; }
static int gkd_ui_freeze_submit_ioctl(struct x1830_fb *f, unsigned long a)
{ (void)f; (void)a; return -ENOTTY; }
static int gkd_ui_freeze_clear_ioctl(struct x1830_fb *f)
{ (void)f; return -ENOTTY; }
static void x1830_debug_frozen_release_locked(struct x1830_fb *f);
static void x1830_menu_unfreeze_locked(struct x1830_fb *f) { (void)f; ++unfreezes; }
static void x1830_volume_osd_clear_locked(struct x1830_fb *f) { (void)f; abort(); }
static void x1830_menu_render_locked(struct x1830_fb *f) { (void)f; abort(); }
static unsigned long x1830_menu_next_deadline_locked(struct x1830_fb *f) { (void)f; abort(); }
/* DRIVER_FRAGMENT */
static struct x1830_fb fb;
static u16 dma_stage[320U * 240U];
static void fixture_dma_bind(struct x1830_fb *f)
{ f->dma=(struct x1830_dma_compositor){.source=f->fb_virt,.output=f->composite_virt,.stage=dma_stage,.output_dma=(u32)f->composite_dma,.frame_bytes=X1830_FB_FRAME_SIZE};
  dma_prepare_calls=dma_cached_calls=dma_finish_calls=dma_direct_calls=dma_stage_cpu_calls=0;
  dma_direct_error=dma_finish_error=0; }
static struct fb_info info = {&fb};
static struct pid owner, competitor;
static struct gkd_ui_plane_submit packet;
static u32 backing[GKD_UI_PLANE_PIXELS];
static unsigned cases;
static int submit(void) { return x1830_fb_ioctl(&info, GKD_UI_PLANE_SUBMIT, (unsigned long)&packet); }
static void rejected(int expected)
{
    struct x1830_fb before = fb;
    u32 pixels[GKD_UI_PLANE_PIXELS];
    int refs = owner.refs, other_refs = competitor.refs;
    memcpy(pixels, backing, sizeof(pixels));
    assert(submit() == expected);
    assert(!memcmp(&fb, &before, sizeof(fb)) && !memcmp(pixels, backing, sizeof(pixels)));
    assert(owner.refs == refs && competitor.refs == other_refs && !allocations);
    ++cases;
}
static void paint(unsigned expected)
{
    u16 pixels[320U * 240U];
    for (size_t i = 0; i < 320U * 240U; ++i) pixels[i] = 0x001f;
    mutex_lock(&fb.volume_osd_lock);
    x1830_user_plane_blend_locked(&fb, pixels);
    mutex_unlock(&fb.volume_osd_lock);
    for (unsigned y = 0; y < 240U; ++y) for (unsigned x = 0; x < 320U; ++x) {
        bool inside = x >= 8U && x < 156U && y >= 206U && y < 225U;
        assert(pixels[y * 320U + x] == (inside ? gkd_ui_blend565_value(0x001f, 0xf800, expected) : 0x001f));
    }
    ++cases;
}
static void fresh_pages(void)
{
    u16 *pages=malloc(X1830_FB_SIZE), *before=malloc(X1830_FB_SIZE);
    u16 *output=malloc(X1830_FB_SIZE), *frozen=malloc(X1830_FB_FRAME_SIZE);
    assert(pages && before && output && frozen);
    for(unsigned i=0;i<320U*240U;++i) {
        pages[i]=(u16)i; pages[i+320U*240U]=(u16)(i^0x5a5aU); frozen[i]=0x07e0;
    }
    memcpy(before,pages,X1830_FB_SIZE);
    fb.fb_dma=0x2000; fb.composite_dma=0x100000; fb.fb_virt=pages;
    fb.composite_virt=output; fb.usb_debug_frozen_virt=frozen; fixture_dma_bind(&fb);
    task.tgid=&owner; jiffies=100; packet.sequence=1; packet.fade_ms=0;
    assert(!submit());
    /* Old state deliberately active: A must still use only its new plane. */
    fb.volume_osd_expires=fb.menu_expires=fb.power_osd_expires=1000;
    fb.menu_view=X1830_MENU_DEBUG;
    for(unsigned pass=0;pass<4;++pass) {
        unsigned page=pass&1U; const u16 *base=pages+page*320U*240U;
        fb.usb_debug_frozen_valid=pass==2;
        if(pass==2) base=frozen;
        if(pass==3) jiffies=(uint32_t)fb.user_plane_expires;
        unsigned before_direct=dma_direct_calls, before_prepare=dma_prepare_calls;
        unsigned before_cached=dma_cached_calls, before_finish=dma_finish_calls;
        mutex_lock(&fb.volume_osd_lock);
        u32 address; assert(!x1830_software_composite_locked(&fb,(u32)fb.fb_dma+page*X1830_FB_FRAME_SIZE,&address));
        mutex_unlock(&fb.volume_osd_lock);
        if (pass == 2) {
            assert(dma_direct_calls==before_direct && dma_prepare_calls==before_prepare &&
                   dma_cached_calls==before_cached+1U && dma_finish_calls==before_finish+1U);
        } else {
            assert(dma_direct_calls==before_direct+1U && dma_prepare_calls==before_prepare &&
                   dma_cached_calls==before_cached && dma_finish_calls==before_finish);
        }
        assert(address==fb.composite_dma+fb.composite_front*X1830_FB_FRAME_SIZE);
        const u16 *visible=output+fb.composite_front*320U*240U;
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
            bool inside=x>=8U&&x<156U&&y>=206U&&y<225U&&pass!=3;
            assert(visible[y*320U+x]==(inside?0xf800:base[y*320U+x]));
        }
        assert(!memcmp(before,pages,X1830_FB_SIZE));
        for(unsigned i=0;i<320U*240U;++i) assert(frozen[i]==0x07e0);
    }
    assert(!owner.refs && !fb.user_plane_active);
    { u32 address; assert(x1830_software_composite_locked(&fb,0x1000,&address)==-EINVAL); }
    assert(!memcmp(before,pages,X1830_FB_SIZE));
    free(frozen); free(output); free(before); free(pages); ++cases;
}
static void debug_expiry(void)
{
    u16 frozen[320U*240U];
    memset(frozen,0xa5,sizeof(frozen)); fb.usb_debug_frozen_virt=frozen;
    fb.usb_debug_frozen_valid=true; fb.menu_view=X1830_MENU_DEBUG;
    fb.menu_expires=51; jiffies=50;
    unsigned old_reassert=reasserts, old_schedule=schedules;
    x1830_volume_osd_clear_work(&fb.volume_osd_clear_work.work);
    assert(fb.usb_debug_frozen_valid && schedules==old_schedule+1U && last_delay>0U);
    jiffies=51; x1830_volume_osd_clear_work(&fb.volume_osd_clear_work.work);
    assert(!fb.usb_debug_frozen_valid && fb.menu_view==X1830_MENU_HIDDEN && unfreezes==1U);
    assert(reasserts>old_reassert);
    for(unsigned i=0;i<320U*240U;++i) assert(!frozen[i]);
    fb.menu_view=X1830_MENU_LOADING; fb.menu_expires=10; jiffies=20;
    fb.usb_debug_frozen_valid=true; fb.usb_debug_loading_active=true;
    fb.usb_debug_loading_hard_expires=100; old_schedule=schedules;
    x1830_volume_osd_clear_work(&fb.volume_osd_clear_work.work);
    assert(fb.usb_debug_loading_held && fb.menu_expires==100 && schedules==old_schedule+1U);
    jiffies=100; old_reassert=reasserts;
    x1830_volume_osd_clear_work(&fb.volume_osd_clear_work.work);
    assert(!fb.usb_debug_frozen_valid && !fb.usb_debug_loading_active && warnings==1U);
    assert(reasserts==old_reassert+1U);
    fb.volume_osd_removing=true; old_reassert=reasserts; old_schedule=schedules;
    x1830_volume_osd_clear_work(&fb.volume_osd_clear_work.work);
    assert(reasserts==old_reassert && schedules==old_schedule);
    fb.volume_osd_removing=false; fb.usb_debug_frozen_virt=NULL; cases+=2U;
}

static struct gkd_ui_menu_submit menu_packet;
static int menu_submit(void)
{ return x1830_fb_ioctl(&info, GKD_UI_MENU_SUBMIT, (unsigned long)&menu_packet); }
static int menu_clear(void)
{ return x1830_fb_ioctl(&info, GKD_UI_MENU_CLEAR, 0); }
static void menu_rejected(int expected)
{
    struct x1830_fb before=fb;
    unsigned alloc_before=allocations;
    int refs=owner.refs, other_refs=competitor.refs;
    u16 *pixels=NULL;
    if (fb.user_menu_virt) {
        pixels=malloc(GKD_UI_MENU_BYTES); assert(pixels);
        memcpy(pixels,fb.user_menu_virt,GKD_UI_MENU_BYTES);
    }
    assert(menu_submit()==expected);
    assert(!memcmp(&fb,&before,sizeof(fb)));
    if (pixels) assert(!memcmp(pixels,fb.user_menu_virt,GKD_UI_MENU_BYTES));
    free(pixels);
    assert(owner.refs==refs && competitor.refs==other_refs && allocations==alloc_before);
    ++cases;
}

static void menu_transport(void)
{
    struct gkd_ui_menu_caps caps;
    struct desc_block descs={0};
    u16 *pages=malloc(X1830_FB_SIZE), *output=malloc(X1830_FB_SIZE);
    u16 *frozen=malloc(X1830_FB_FRAME_SIZE), *saved=malloc(X1830_FB_SIZE);
    assert(pages && output && frozen && saved);
    memset(&fb,0,sizeof(fb)); fb.user_plane_virt=backing;
    fb.descs=&descs; fb.fb_dma=0x2000; fb.composite_dma=0x100000;
    fb.fb_virt=pages; fb.composite_virt=output; fb.usb_debug_frozen_virt=frozen; fixture_dma_bind(&fb);
    for(unsigned i=0;i<GKD_UI_MENU_PIXELS;++i) {
        pages[i]=(u16)(i^0x1234); pages[i+GKD_UI_MENU_PIXELS]=(u16)(i^0x789a);
        frozen[i]=(u16)(i^0xf00f); menu_payload[i]=(u16)(i^0x6789);
    }
    memcpy(saved,pages,X1830_FB_SIZE);
    task.tgid=&owner; jiffies=100;
    menu_packet=(struct gkd_ui_menu_submit){.pixels=0x2000,.pixel_bytes=GKD_UI_MENU_BYTES,.ttl_ms=1000,.sequence=1};
    assert(sizeof(menu_packet)==32 && sizeof(caps)==32);
    assert(_IOC_SIZE(GKD_UI_MENU_SUBMIT)==32 && _IOC_SIZE(GKD_UI_MENU_GET_CAPS)==32);
    output_fault=1;
    assert(x1830_fb_ioctl(&info,GKD_UI_MENU_GET_CAPS,(unsigned long)&caps)==-EFAULT);
    output_fault=0;
    memset(&caps,0xa5,sizeof(caps));
    assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_GET_CAPS,(unsigned long)&caps));
    assert(caps.abi==2 && caps.width==320 && caps.height==240 &&
           caps.format==GKD_UI_MENU_RGB565 && caps.pixel_bytes==153600 &&
           caps.min_ttl_ms==20 && caps.max_ttl_ms==10000 && caps.max_transition_ms==1000);
    ++cases;
    unsigned count=captures;
    cap=0; menu_rejected(-EPERM); assert(menu_clear()==-EPERM); cap=1;
    header_fault=1; menu_rejected(-EFAULT); header_fault=0;
    for(unsigned i=0;i<2;++i) { menu_packet.reserved[i]=1; menu_rejected(-EINVAL); menu_packet.reserved[i]=0; }
    menu_packet.pixels=0; menu_rejected(-EINVAL);
    menu_packet.pixels=UINT64_C(1)<<40; menu_rejected(-EINVAL); menu_packet.pixels=0x2000;
    menu_packet.pixel_bytes--; menu_rejected(-EINVAL); menu_packet.pixel_bytes++;
    menu_packet.ttl_ms=19; menu_rejected(-EINVAL); menu_packet.ttl_ms=10001; menu_rejected(-EINVAL); menu_packet.ttl_ms=1000;
    menu_packet.sequence=0; menu_rejected(-EINVAL); menu_packet.sequence=1;
    assert(captures==count);
    menu_packet.pixels=0x2001; menu_rejected(-EFAULT); menu_packet.pixels=0x2000;
    payload_error=ENOMEM; menu_rejected(-ENOMEM); payload_error=EFAULT; menu_rejected(-EFAULT); payload_error=0;
    mutate_after_copy=1; count=captures; assert(!menu_submit()); mutate_after_copy=0;
    assert(captures==count+1 && allocations==1 && owner.refs==1);
    assert(fb.user_menu_virt[0]==0x6789 && menu_payload[0]==0xeeee); ++cases;
    task.tgid=&competitor; menu_rejected(-EBUSY); assert(menu_clear()==-EBUSY);
    task.tgid=&owner; menu_rejected(-ESTALE); menu_packet.sequence=2;
    payload_error=ENOMEM; menu_rejected(-ENOMEM); payload_error=EFAULT; menu_rejected(-EFAULT); payload_error=0;
    header_fault=1; menu_rejected(-EFAULT); header_fault=0;
    fb.volume_osd_removing=true; menu_rejected(-ENODEV); assert(menu_clear()==-ENODEV); fb.volume_osd_removing=false;
    for(unsigned i=0;i<GKD_UI_MENU_PIXELS;++i) menu_payload[i]=(u16)(i^0x3210);
    fb.composite_active=true; count=schedules; assert(!menu_submit());
    assert(allocations==1 && owner.refs==1 && schedules==count+1); ++cases;
    for(unsigned page=0;page<2;++page) {
        fb.composite_source_addr=(u32)fb.fb_dma+page*X1830_FB_FRAME_SIZE;
        unsigned before_direct=dma_direct_calls, before_prepare=dma_prepare_calls;
        unsigned before_cached=dma_cached_calls, before_finish=dma_finish_calls;
        x1830_composite_refresh_work(&fb.composite_work.work);
        assert(dma_direct_calls==before_direct && dma_prepare_calls==before_prepare &&
               dma_cached_calls==before_cached+1U && dma_finish_calls==before_finish+1U);
        assert(fb.composite_active && descs.layer[0].buffer_addr==fb.composite_dma+fb.composite_front*X1830_FB_FRAME_SIZE);
        assert(!memcmp(output+fb.composite_front*GKD_UI_MENU_PIXELS,menu_payload,GKD_UI_MENU_BYTES));
        assert(!memcmp(saved,pages,X1830_FB_SIZE)); ++cases;
    }
    /* Independent OSD publisher remains usable above a different menu owner. */
    task.tgid=&competitor; packet.sequence=1; packet.fade_ms=0; packet.ttl_ms=1000;
    assert(!submit()); assert(owner.refs==1 && competitor.refs==1);
    x1830_composite_refresh_work(&fb.composite_work.work);
    const u16 *visible=output+fb.composite_front*GKD_UI_MENU_PIXELS;
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
        unsigned i=y*320+x;
        assert(visible[i]==((x>=8 && x<156 && y>=206 && y<225)?0xf800:menu_payload[i]));
    }
    assert(!memcmp(saved,pages,X1830_FB_SIZE)); ++cases;
    task.tgid=&owner;
    assert(!menu_clear() && !allocations && !owner.refs && competitor.refs==1);
    x1830_composite_refresh_work(&fb.composite_work.work);
    visible=output+fb.composite_front*GKD_UI_MENU_PIXELS;
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
        unsigned i=y*320+x;
        assert(visible[i]==((x>=8 && x<156 && y>=206 && y<225)?0xf800:pages[i+GKD_UI_MENU_PIXELS]));
    }
    ++cases;
    task.tgid=&competitor; assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0));
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!fb.composite_active && descs.layer[0].buffer_addr==fb.composite_source_addr);
    assert(!menu_clear()); task.tgid=&owner;
    /* TTL remains bounded over MIPS 32-bit jiffies wrap, restores current page. */
    jiffies=UINT32_MAX-5U; menu_packet.sequence=1; menu_packet.ttl_ms=100;
    assert(!menu_submit()); jiffies=2;
    x1830_composite_refresh_work(&fb.composite_work.work); assert(fb.composite_active);
    jiffies=(uint32_t)fb.user_menu_expires;
    fb.composite_source_addr=(u32)fb.fb_dma;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!fb.composite_active && descs.layer[0].buffer_addr==fb.fb_dma);
    assert(!allocations && !owner.refs && !fb.user_menu_virt); ++cases;
    /* Sequence exhaustion cannot wrap a live lease; competitor can take over only after expiry. */
    menu_packet.sequence=UINT32_MAX; menu_packet.ttl_ms=20; assert(!menu_submit());
    menu_packet.sequence=1; menu_rejected(-ESTALE);
    task.tgid=&competitor; menu_rejected(-EBUSY);
    jiffies=(uint32_t)fb.user_menu_expires; assert(!menu_submit());
    assert(!owner.refs && competitor.refs==1 && allocations==1); assert(!menu_clear()); ++cases;
    /* Ending the final UI lease must keep accepted DEBUG frozen underlay. */
    task.tgid=&owner;
    for(unsigned pass=0;pass<2;++pass) {
        fb.usb_debug_frozen_valid=true; menu_packet.sequence=1; assert(!menu_submit());
        if(pass) jiffies=(uint32_t)fb.user_menu_expires;
        else assert(!menu_clear());
        x1830_composite_refresh_work(&fb.composite_work.work);
        assert(fb.composite_active && !allocations && !owner.refs);
        assert(!memcmp(output+fb.composite_front*GKD_UI_MENU_PIXELS,frozen,GKD_UI_MENU_BYTES));
        assert(!memcmp(saved,pages,X1830_FB_SIZE)); ++cases;
        fb.usb_debug_frozen_valid=false;
        x1830_composite_refresh_work(&fb.composite_work.work);
        assert(!fb.composite_active && descs.layer[0].buffer_addr==fb.composite_source_addr);
    }
    /* Expire both independently: neither live plane prevents the other's reap. */
    assert(!menu_submit()); packet.sequence=1; packet.ttl_ms=1000; assert(!submit());
    jiffies=(uint32_t)fb.user_menu_expires;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!fb.user_menu_owner && fb.user_plane_active && owner.refs==1 && !allocations);
    jiffies=(uint32_t)fb.user_plane_expires;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!owner.refs && !fb.composite_active); ++cases;
    menu_packet.ttl_ms=1000; assert(!menu_submit()); packet.sequence=1; packet.ttl_ms=20; assert(!submit());
    jiffies=(uint32_t)fb.user_plane_expires;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(fb.user_menu_owner && !fb.user_plane_active && owner.refs==1 && allocations==1);
    /* Removal release is serialized under the same mutex; queued work cannot draw. */
    fb.volume_osd_removing=true; count=schedules;
    x1830_composite_refresh_work(&fb.composite_work.work); assert(schedules==count);
    mutex_lock(&fb.volume_osd_lock);
    x1830_user_menu_release_locked(&fb); x1830_user_plane_release_locked(&fb);
    mutex_unlock(&fb.volume_osd_lock);
    assert(!allocations && !owner.refs && !competitor.refs); ++cases;
    free(saved); free(frozen); free(output); free(pages);
}


static void motion_frame(unsigned rows, int osd)
{
    x1830_composite_refresh_work(&fb.composite_work.work);
    const u16 *visible;
    if (fb.composite_active)
        visible=(const u16 *)fb.composite_virt+fb.composite_front*GKD_UI_MENU_PIXELS;
    else {
        assert(fb.descs->layer[0].buffer_addr==fb.composite_source_addr);
        visible=(const u16 *)fb.fb_virt+(fb.composite_source_addr-(u32)fb.fb_dma)/2U;
    }
    const u16 *under=fb.usb_debug_frozen_valid ? fb.usb_debug_frozen_virt :
        (const u16 *)fb.fb_virt+(fb.composite_source_addr-(u32)fb.fb_dma)/2U;
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x) {
        u16 expected=y<rows ? menu_payload[(240U-rows+y)*320U+x] : under[y*320U+x];
        if(osd && x>=8 && x<156 && y>=206 && y<225) expected=0xf800;
        assert(visible[y*320U+x]==expected);
    }
    ++cases;
}
static void menu_motion(void)
{
    struct desc_block descs={0};
    u16 *pages=malloc(X1830_FB_SIZE), *output=malloc(X1830_FB_SIZE);
    u16 *frozen=malloc(X1830_FB_FRAME_SIZE), *saved=malloc(X1830_FB_SIZE);
    assert(pages && output && frozen && saved);
    memset(&fb,0,sizeof(fb)); fb.user_plane_virt=backing;
    fb.descs=&descs; fb.fb_dma=0x2000; fb.composite_dma=0x100000;
    fb.fb_virt=pages; fb.composite_virt=output; fb.usb_debug_frozen_virt=frozen; fixture_dma_bind(&fb);
    fb.composite_source_addr=(u32)fb.fb_dma;
    for(unsigned i=0;i<GKD_UI_MENU_PIXELS;++i) {
        pages[i]=(u16)(i^0x1234); pages[i+GKD_UI_MENU_PIXELS]=(u16)(i^0x789a);
        frozen[i]=(u16)(i^0xf00f); menu_payload[i]=(u16)(i^0x3210);
    }
    memcpy(saved,pages,X1830_FB_SIZE); task.tgid=&owner; jiffies=100;
    menu_packet=(struct gkd_ui_menu_submit){.pixels=0x2000,.pixel_bytes=GKD_UI_MENU_BYTES,
        .ttl_ms=1000,.sequence=1,.transition_ms=1001};
    menu_rejected(-EINVAL); menu_packet.transition_ms=200;
    assert(!menu_submit()); motion_frame(0,0);
    unsigned before_direct=dma_direct_calls, before_prepare=dma_prepare_calls;
    unsigned before_finish=dma_finish_calls;
    jiffies=105; motion_frame(60,0);
    assert(dma_direct_calls==before_direct && dma_prepare_calls==before_prepare+1U &&
           dma_finish_calls==before_finish+1U);
    fb.composite_source_addr=(u32)fb.fb_dma+X1830_FB_FRAME_SIZE;
    jiffies=110; motion_frame(120,0);
    fb.usb_debug_frozen_valid=true; jiffies=115; motion_frame(180,0);
    jiffies=120; motion_frame(240,0);
    /* OSD is anchored to the screen, not the sliding menu coordinate space. */
    task.tgid=&competitor; packet.sequence=1; packet.fade_ms=0; packet.ttl_ms=5000;
    assert(!submit()); motion_frame(240,1);
    assert(x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)==-EBUSY);
    task.tgid=&owner;
    cap=0; assert(x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)==-EPERM); cap=1;
    jiffies=125; assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)); motion_frame(240,1);
    unsigned long expiry=fb.user_menu_expires;
    jiffies=130; assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0));
    assert(fb.user_menu_expires==expiry); motion_frame(180,1);
    /* Reopening halfway through an exit starts at the same visible rows. */
    menu_packet.sequence=2; assert(!menu_submit());
    assert(!fb.user_menu_closing && fb.user_menu_from_rows==180 && fb.user_menu_animation_ms==50);
    motion_frame(180,1); jiffies=132; motion_frame(204,1); jiffies=135; motion_frame(240,1);
    /* A later TTL automatically slides away even if the publisher disappears. */
    jiffies=220; motion_frame(120,1);
    jiffies=230; motion_frame(0,1); assert(!fb.user_menu_owner && !allocations && owner.refs==0);
    task.tgid=&competitor; assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0)); task.tgid=&owner;
    motion_frame(0,0); /* Frozen DEBUG underlay persists when the final UI ends. */
    fb.usb_debug_frozen_valid=false;
    /* Mid-entry close is continuous, bounded, and repeat HIDE cannot extend it. */
    jiffies=300; menu_packet.sequence=1; assert(!menu_submit());
    jiffies=310; motion_frame(120,0); assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0));
    assert(fb.user_menu_animation_ms==100); motion_frame(120,0);
    jiffies=315; motion_frame(60,0);
    jiffies=320; motion_frame(0,0); assert(!allocations && !owner.refs);
    /* Disabled motion stays opaque until expiry, HIDE and CLEAR are immediate. */
    jiffies=400; menu_packet.sequence=1; menu_packet.transition_ms=0; assert(!menu_submit());
    motion_frame(240,0); jiffies=499; motion_frame(240,0); jiffies=500; motion_frame(0,0);
    menu_packet.sequence=1; assert(!menu_submit()); assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0));
    motion_frame(0,0); assert(!owner.refs && !allocations);
    assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)); ++cases;
    menu_packet.sequence=1; menu_packet.transition_ms=200; assert(!menu_submit());
    assert(!menu_clear()); motion_frame(0,0);
    /* Repeated effect-on/off changes during a live lease use one engine. */
    jiffies=600; menu_packet.sequence=1; assert(!menu_submit());
    jiffies=605; motion_frame(60,0);
    menu_packet.sequence=2; menu_packet.transition_ms=0; assert(!menu_submit()); motion_frame(240,0);
    menu_packet.sequence=3; menu_packet.transition_ms=200; assert(!menu_submit()); motion_frame(240,0);
    assert(!menu_clear());
    /* Exact tick-zero/wrap and minimum TTL must neither underflow nor retain leases. */
    jiffies=UINT32_MAX-5U; menu_packet.sequence=1; assert(!menu_submit());
    jiffies=2; motion_frame(96,0);
    jiffies=14; motion_frame(240,0); assert(!menu_clear());
    jiffies=0; menu_packet.sequence=1; menu_packet.ttl_ms=20; assert(!menu_submit()); motion_frame(0,0);
    jiffies=1; motion_frame(12,0);
    unsigned long bounded=fb.user_menu_expires;
    assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)); assert(fb.user_menu_expires<=bounded);
    jiffies=2; motion_frame(0,0); assert(!allocations && !owner.refs);
    fb.volume_osd_removing=true; assert(x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)==-ENODEV);
    fb.volume_osd_removing=false;
    /* A tick between rows sampling and HIDE start cannot shorten the lease
     * without also shortening the remaining animation span. */
    jiffies=1000; menu_packet.sequence=1; menu_packet.ttl_ms=1000;
    assert(!menu_submit()); jiffies=1098; motion_frame(24,0);
    clock_reads=0; advance_on_read=3; advance_to=1099;
    assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0));
    advance_on_read=0;
    assert(fb.user_menu_started==1099 && fb.user_menu_expires==1100 &&
           fb.user_menu_animation_ms==10 && fb.user_menu_from_rows==24);
    motion_frame(24,0); jiffies=1100; motion_frame(0,0);
    /* Same race crossing expiry releases immediately and remains idempotent. */
    jiffies=1200; menu_packet.sequence=1; assert(!menu_submit()); jiffies=1299;
    clock_reads=0; advance_on_read=3; advance_to=1300;
    assert(!x1830_fb_ioctl(&info,GKD_UI_MENU_HIDE,0)); advance_on_read=0;
    assert(!allocations && !owner.refs); motion_frame(0,0);
    assert(!memcmp(saved,pages,X1830_FB_SIZE));
    if(getenv("GKD_MENU_BENCH")) {
        volatile u32 address=0; const unsigned count=10000;
        menu_packet.ttl_ms=1000; menu_packet.sequence=1; menu_packet.transition_ms=200;
        jiffies=100; assert(!menu_submit()); jiffies=120;
        clock_t start=clock();
        for(unsigned i=0;i<count;++i) assert(!x1830_software_composite_locked(&fb,fb.composite_source_addr,(u32 *)&address));
        double steady=1e6*(double)(clock()-start)/(double)CLOCKS_PER_SEC/count;
        jiffies=110; start=clock();
        for(unsigned i=0;i<count;++i) assert(!x1830_software_composite_locked(&fb,fb.composite_source_addr,(u32 *)&address));
        double sliding=1e6*(double)(clock()-start)/(double)CLOCKS_PER_SEC/count;
        printf("GKD_MENU_COPY_HOST_BENCH steady_us=%.3f half_slide_us=%.3f ratio=%.3f frames=%u guard=%u\n",
            steady,sliding,sliding/steady,count,address);
        assert(!menu_clear());
    }
    free(saved); free(frozen); free(output); free(pages);
}

static void dma_fault_publication(void)
{
    struct desc_block descs={0};
    u16 *pages=calloc(1,X1830_FB_SIZE), *output=calloc(1,X1830_FB_SIZE);
    u16 *frozen=calloc(1,X1830_FB_FRAME_SIZE);
    u32 prior_addr=0x45670000; u8 prior_front=1;
    unsigned before_schedule, before_prepare, before_direct;
    assert(pages && output && frozen);
    memset(&fb,0,sizeof(fb)); fb.descs=&descs; fb.fb_dma=0x2000; fb.composite_dma=0x100000;
    fb.fb_virt=pages; fb.composite_virt=output; fb.user_plane_virt=backing;
    fb.composite_source_addr=(u32)fb.fb_dma; fb.composite_front=prior_front;
    fb.user_plane_active=true; fb.user_plane_expires=200; fb.user_plane_started=100;
    fb.user_plane_fade_done=true; descs.layer[0].buffer_addr=prior_addr;
    fixture_dma_bind(&fb); jiffies=100;
    /* A failing direct source-to-inactive-output transfer must not publish. */
    memset(output,0xa5,X1830_FB_SIZE);
    dma_direct_error=-EIO;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(fb.dma.fault==-EIO && fb.composite_front==prior_front &&
           descs.layer[0].buffer_addr==prior_addr);
    dma_direct_error=0;
    before_schedule=schedules; before_prepare=dma_prepare_calls; before_direct=dma_direct_calls;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(fb.composite_front==prior_front && descs.layer[0].buffer_addr==prior_addr);
    assert(schedules==before_schedule+1U && last_delay==msecs_to_jiffies(100) &&
           dma_direct_calls==before_direct);
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(dma_prepare_calls==before_prepare && dma_direct_calls==before_direct && fb.composite_front==prior_front &&
           descs.layer[0].buffer_addr==prior_addr);
    fb.user_plane_expires=100;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(!fb.user_plane_active && dma_prepare_calls==before_prepare &&
           descs.layer[0].buffer_addr==fb.composite_source_addr);
    /* Cached freeze still uses its final DMA and must equally retain front. */
    fixture_dma_bind(&fb); fb.composite_front=prior_front;
    fb.usb_debug_frozen_virt=frozen; fb.usb_debug_frozen_valid=true;
    descs.layer[0].buffer_addr=prior_addr; dma_finish_error=-EIO;
    x1830_composite_refresh_work(&fb.composite_work.work);
    assert(fb.dma.fault==-EIO && dma_cached_calls==1U && dma_finish_calls==1U &&
           fb.composite_front==prior_front && descs.layer[0].buffer_addr==prior_addr);
    free(frozen); free(output); free(pages); ++cases;
}

int main(void)
{
    struct gkd_ui_plane_caps caps;
    fb.user_plane_virt = backing; task.tgid = &owner; jiffies = 100U;
    packet = (struct gkd_ui_plane_submit){.pixels=0x1000U, .pixel_bytes=GKD_UI_PLANE_BYTES,
        .ttl_ms=1000U, .fade_ms=160U, .sequence=1U};
    for (unsigned i=0; i<GKD_UI_PLANE_PIXELS; ++i) payload[i] = 0xffff0000U;
    cap=0; rejected(-EPERM); assert(!captures); cap=1;
    header_fault=1; rejected(-EFAULT); assert(!captures); header_fault=0;
    packet.reserved[0]=1; rejected(-EINVAL); packet.reserved[0]=0;
    packet.reserved[1]=1; rejected(-EINVAL); packet.reserved[1]=0;
    packet.pixels=0; rejected(-EINVAL); packet.pixels=UINT64_C(1)<<40; rejected(-EINVAL);
    packet.pixels=0x1001; rejected(-EFAULT); packet.pixels=0x1000;
    packet.pixel_bytes--; rejected(-EINVAL); packet.pixel_bytes++;
    packet.ttl_ms=19; rejected(-EINVAL); packet.ttl_ms=10001; rejected(-EINVAL); packet.ttl_ms=1000;
    packet.fade_ms=1001; rejected(-EINVAL); packet.fade_ms=160;
    packet.sequence=0; rejected(-EINVAL); packet.sequence=1;
    payload_error=EFAULT; rejected(-EFAULT); payload_error=ENOMEM; rejected(-ENOMEM); payload_error=0;
    mutate_after_copy=1; unsigned prior=captures; assert(!submit()); mutate_after_copy=0;
    assert(captures==prior+1 && !allocations && backing[0]==0xffff0000U && payload[0]==0xeeeeeeeeU);
    assert(owner.refs==1 && fb.user_plane_started==100 && reasserts==1); paint(0);
    for(unsigned i=0;i<GKD_UI_PLANE_PIXELS;++i) payload[i]=0xffff0000U;
    task.tgid=&competitor; rejected(-EBUSY); assert(x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0)==-EBUSY);
    task.tgid=&owner; rejected(-ESTALE); packet.sequence=2;
    header_fault=1; rejected(-EFAULT); header_fault=0; payload_error=EFAULT; rejected(-EFAULT); payload_error=0;
    fb.volume_osd_removing=true; rejected(-ENODEV); assert(x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0)==-ENODEV); fb.volume_osd_removing=false;
    jiffies=102; fb.composite_active=true; assert(!submit()); assert(schedules==1 && owner.refs==1 && fb.user_plane_started==100); paint(31);
    jiffies=116; paint(255); assert(fb.user_plane_fade_done);
    /* No fade restart on repeated long-lived renewals, including tick wrap. */
    jiffies=UINT32_MAX-5U; fb.user_plane_expires=jiffies+100U; packet.sequence=3;
    assert(!submit()); paint(255); jiffies=4; packet.sequence=4; assert(!submit()); paint(255);
    jiffies=(uint32_t)(fb.user_plane_expires-8U); paint(127);
    jiffies=(uint32_t)fb.user_plane_expires; paint(0); assert(!fb.user_plane_active && !owner.refs);
    assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0));
    /* Real tick zero and a fresh lease straddling wrap both fade normally. */
    jiffies=0; packet.sequence=1; assert(!submit()); paint(0); jiffies=2; paint(31);
    cap=0; assert(x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0)==-EPERM); cap=1;
    assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0) && !owner.refs);
    jiffies=UINT32_MAX-5U; assert(!submit()); paint(0); jiffies=2; paint(127);
    task.tgid=&competitor; jiffies=(uint32_t)fb.user_plane_expires; assert(!submit()); assert(!owner.refs && competitor.refs==1);
    assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_CLEAR,0) && !competitor.refs && !allocations);
    output_fault=1; assert(x1830_fb_ioctl(&info,GKD_UI_PLANE_GET_CAPS,(unsigned long)&caps)==-EFAULT); output_fault=0;
    assert(!x1830_fb_ioctl(&info,GKD_UI_PLANE_GET_CAPS,(unsigned long)&caps));
    assert(caps.abi==1 && caps.x==8 && caps.y==206 && caps.width==148 && caps.height==19 && caps.format==GKD_UI_PLANE_ARGB8888);
    assert(x1830_fb_ioctl(&info,0xffffffffU,0)==-ENOTTY);
    fresh_pages();
    debug_expiry();
    menu_transport();
    menu_motion();
    dma_fault_publication();
    printf("GKD_USER_PLANE_FINAL_A=PASS cases=%u capture-once/ABI/faults/owner/refs/CLEAR/fade/wrap/pixel-bounds/full-partial-menu/gamepage/frozen/final-compositor/fault-no-publish/fault-lease-reap/no-dma-retry/DEBUG-expiry-reassert/menu-lease/opaque-frame/coexistence/actual-refresh/slide/rapid-reopen/hide/effects-off\n",cases);
    return 0;
}
