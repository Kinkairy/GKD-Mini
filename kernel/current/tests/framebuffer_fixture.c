/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fb.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint64_t dma_addr_t;
struct x1830_fb { dma_addr_t fb_dma; int volume_osd_lock,volume_osd_removing,usb_debug_freeze; unsigned latest_scanout_addr; void *dev; };
struct fb_info { struct x1830_fb *par; struct fb_var_screeninfo var; };
struct vm_area_struct { unsigned long vm_start,vm_end,vm_pgoff,vm_flags,vm_page_prot; };
#define PAGE_ALIGN(x) (((x)+4095UL)&~4095UL)
#define PHYS_PFN(x) ((x)>>12)
#define VM_IO 1
#define VM_DONTEXPAND 2
#define VM_DONTDUMP 4
#define dma_to_phys(dev,x) (x)
#define pgprot_writecombine(x) (x)
#define upper_32_bits(x) ((x)>>32)
#define lower_32_bits(x) ((unsigned)(x))
static void mutex_lock(int *p) { ++*p; }
static void mutex_unlock(int *p) { --*p; }
static unsigned shown, mapped;
static int x1830_hardware_osd_reassert_locked(struct x1830_fb *fb,unsigned addr) { assert(fb->volume_osd_lock==1);shown=addr;return 0; }
static int io_remap_pfn_range(struct vm_area_struct *v,unsigned long start,unsigned long pfn,unsigned long size,unsigned long prot) { (void)v;(void)start;(void)pfn;(void)prot;mapped=size;return 0; }
/* DRIVER_DEFINES */
/* DRIVER_FUNCTIONS */
static struct fb_var_screeninfo mode(unsigned height,unsigned offset) {
 struct fb_var_screeninfo v={0}; v.xres=320;v.yres=240;v.bits_per_pixel=16;v.yres_virtual=height;v.yoffset=offset;return v;
}
int main(void) {
 struct x1830_fb fb={.fb_dma=0x100000};
 struct fb_info info={.par=&fb};
 assert(X1830_FB_SIZE==460800);
 unsigned heights[]={240,480,720};
 for(unsigned i=0;i<3;i++) {
  struct fb_var_screeninfo v=mode(heights[i],0);
  assert(x1830_fb_check_var(&v,&info)==0);
  assert(v.yres_virtual==(i==2?720:480));info.var=v;
  assert(v.xres_virtual==320 && v.red.offset==11 && v.green.length==6 && v.blue.length==5);
  for(unsigned offset=0;offset<960;offset++) {
   v.yoffset=offset;shown=0;
   int valid=offset%240==0 && offset+240<=info.var.yres_virtual;
   assert((x1830_fb_pan_display(&v,&info)==0)==valid);
   if(valid) assert(shown==0x100000+offset*640);
  }
 }
 struct fb_var_screeninfo bad=mode(960,0);assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 bad=mode(480,480);assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 bad=mode(720,1);assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 bad=mode(720,480);assert(x1830_fb_check_var(&bad,&info)==0);
 bad.bits_per_pixel=32;assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 bad=mode(720,0);bad.xres=640;assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 bad=mode(720,0);bad.xoffset=1;assert(x1830_fb_check_var(&bad,&info)==-EINVAL);
 for(unsigned frames=1;frames<=3;frames++) {
  struct vm_area_struct v={.vm_end=PAGE_ALIGN(frames*153600)};
  assert(x1830_fb_mmap(&info,&v)==0 && mapped==PAGE_ALIGN(frames*153600));
 }
 struct vm_area_struct v={.vm_end=PAGE_ALIGN(X1830_FB_SIZE)+4096};
 assert(x1830_fb_mmap(&info,&v)==-EINVAL);
 v.vm_end=4096;v.vm_pgoff=1;assert(x1830_fb_mmap(&info,&v)==-EINVAL);
 puts("FRAMEBUFFER_CONTRACT=PASS double/triple negotiated; 2880 pan boundaries; mmap bounds");
}
