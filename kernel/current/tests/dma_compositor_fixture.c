/* Actual helper bodies are injected below; transport is a bounded host fake. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint64_t dma_addr_t;
typedef int dma_cookie_t;
struct device {}; struct dma_chan {}; struct completion { int done; };
struct dma_tx_state {};
enum dma_data_direction { DMA_BIDIRECTIONAL, DMA_TO_DEVICE, DMA_FROM_DEVICE };
enum dma_status { DMA_COMPLETE, DMA_ERROR };
struct dma_async_tx_descriptor { void (*callback)(void *); void *callback_param; };
struct x1830_dma_mapping {
 dma_addr_t address,native; size_t bytes; enum dma_data_direction direction;
 bool table,mapped;
};
struct x1830_dma_compositor {
 struct device *dev,*copy_dev; struct dma_chan *channel; struct completion done;
 struct x1830_dma_mapping source,output;
 void *stage; dma_addr_t stage_dma; size_t frame_bytes,stage_bytes;
 bool stage_cpu,paused; int fault;
};
#define X1830_DMA_TIMEOUT_MS 20U
#define DMA_PREP_INTERRUPT 1U
#define DMA_CTRL_ACK 2U
#define lower_32_bits(x) ((u32)(x))
#define msecs_to_jiffies(x) (x)
#define dma_wmb() ((void)0)
#define dma_rmb() ((void)0)
#define dev_err(...) ((void)0)
enum fake_mode { OK, PREP_NULL, SUBMIT_ERROR, TIMEOUT, STATUS_ERROR };
static enum fake_mode mode; static struct dma_async_tx_descriptor descriptor;
static dma_addr_t dst,src; static size_t length; static unsigned prep,term,to_dev,to_cpu;
static bool callback_called, submitted;
static void reinit_completion(struct completion *x){x->done=0;}
static void complete(struct completion *x){x->done=1;}
static struct dma_async_tx_descriptor *dmaengine_prep_dma_memcpy(struct dma_chan *c,dma_addr_t d,dma_addr_t s,size_t n,unsigned f)
{ (void)c;(void)f;prep++;callback_called=false;dst=d;src=s;length=n;memset(&descriptor,0,sizeof(descriptor));if(mode==PREP_NULL)return NULL;return &descriptor; }
static dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *x)
{ (void)x;if(mode==SUBMIT_ERROR)return -EIO;submitted=true;return 1; }
static int dma_submit_error(dma_cookie_t x){return x<0?x:0;}
static void dma_async_issue_pending(struct dma_chan *x)
{ (void)x;if(mode!=TIMEOUT){if(mode==OK)memcpy((void *)(uintptr_t)dst,(void *)(uintptr_t)src,length);if(mode==STATUS_ERROR)memcpy((void *)(uintptr_t)dst,(void *)(uintptr_t)src,length/2U);descriptor.callback(descriptor.callback_param);callback_called=true;} }
static unsigned long wait_for_completion_timeout(struct completion *x,unsigned long y)
{ (void)y;return x->done&&mode!=TIMEOUT; }
static enum dma_status dmaengine_tx_status(struct dma_chan *x,dma_cookie_t y,struct dma_tx_state *z)
{ (void)x;(void)y;(void)z;return mode==STATUS_ERROR?DMA_ERROR:DMA_COMPLETE; }
static int dmaengine_terminate_sync(struct dma_chan *x)
{ (void)x;term++;if(submitted&&!callback_called&&descriptor.callback){descriptor.callback(descriptor.callback_param);callback_called=true;}submitted=false;return 0; }
static void dma_sync_single_for_cpu(struct device *d,dma_addr_t a,size_t n,enum dma_data_direction x)
{ (void)d;(void)a;(void)n;(void)x;to_cpu++; }
static void dma_sync_single_for_device(struct device *d,dma_addr_t a,size_t n,enum dma_data_direction x)
{ (void)d;(void)a;(void)n;(void)x;to_dev++; }
/* DRIVER_FRAGMENT */
static void reset_copy(struct x1830_dma_compositor *c,void *stage,void *source,void *output,size_t bytes)
{ static struct device d;static struct dma_chan h;memset(c,0,sizeof(*c));c->dev=c->copy_dev=&d;c->channel=&h;c->stage=stage;c->stage_dma=(uintptr_t)stage;c->source.address=(uintptr_t)source;c->source.bytes=bytes;c->output.address=c->output.native=(uintptr_t)output;c->output.bytes=bytes;c->frame_bytes=c->stage_bytes=bytes;c->stage_cpu=true;mode=OK;prep=term=to_dev=to_cpu=0;callback_called=submitted=false; }
static void direct_contract(void)
{ unsigned char in[32],stage[32],out[32];struct x1830_dma_compositor c;unsigned before;
  for(unsigned i=0;i<32;i++) in[i]=(unsigned char)i;
  memset(out,0xa5,32);
  reset_copy(&c,stage,in,out,32);c.frame_bytes=16;
  assert(!x1830_dma_copy_output(&c,16,0));assert(!memcmp(out,in+16,16));
  assert(!x1830_dma_copy_output(&c,0,16));assert(!memcmp(out+16,in,16));
  assert(prep==2&&term==2&&to_dev==0&&to_cpu==0&&!submitted);
  before=prep;assert(x1830_dma_copy_output(&c,17,0)==-EINVAL);
  assert(x1830_dma_copy_output(&c,0,17)==-EINVAL&&prep==before&&!c.fault);
  c.source.bytes=15;assert(x1830_dma_copy_output(&c,0,0)==-EINVAL&&prep==before);
  c.source.bytes=32;c.output.bytes=15;assert(x1830_dma_copy_output(&c,0,0)==-EINVAL&&prep==before);
  c.output.bytes=32;assert(x1830_dma_copy_output(&c,SIZE_MAX,0)==-EINVAL&&prep==before);
  assert(x1830_dma_copy_output(&c,0,SIZE_MAX)==-EINVAL&&prep==before);
  c.paused=true;assert(x1830_dma_copy_output(&c,0,0)==-EAGAIN&&prep==before); }
static void direct_failure(enum fake_mode bad)
{ unsigned char in[32]={1},stage[32],out[32];struct x1830_dma_compositor c;unsigned after;
  memset(out,0xa5,32);reset_copy(&c,stage,in,out,32);mode=bad;
  assert(x1830_dma_copy_output(&c,0,0)<0);after=prep;
  if(bad==TIMEOUT)assert(callback_called&&c.done.done);
  assert(c.fault&&term==1&&!submitted);
  assert(x1830_dma_copy_output(&c,0,0)==c.fault&&prep==after);
  if(bad==STATUS_ERROR)assert(out[0]==1&&out[31]==0xa5);else assert(out[0]==0xa5); }
static void failure(enum fake_mode bad)
{ unsigned char in[32]={1},stage[32],out[32];struct x1830_dma_compositor c;u32 published=0xfeedbeef;unsigned after;memset(stage,0xcc,32);memset(out,0xa5,32);reset_copy(&c,stage,in,out,32);assert(!x1830_dma_prepare(&c,0));mode=bad;assert(x1830_dma_finish(&c,0,&published)<0);after=prep;if(bad==TIMEOUT)assert(callback_called&&c.done.done);assert(c.fault&&term&&published==0xfeedbeef);assert(x1830_dma_prepare(&c,0)==c.fault&&x1830_dma_finish(&c,0,&published)==c.fault&&x1830_dma_prepare_cached(&c,in)==c.fault&&prep==after);if(bad==STATUS_ERROR)assert(out[0]==1&&out[31]==0xa5);else assert(out[0]==0xa5);assert(!submitted); }
int main(void)
{ unsigned char in[32],stage[32],out[32],frozen[32];struct x1830_dma_compositor c;u32 address=0;for(unsigned i=0;i<32;i++)in[i]=i;reset_copy(&c,stage,in,out,32);assert(!x1830_dma_prepare_cached(&c,in)&&!memcmp(in,stage,32));assert(!x1830_dma_prepare(&c,0)&&to_dev==1&&to_cpu==1&&!memcmp(in,stage,32));assert(!x1830_dma_finish(&c,0,&address)&&address==(u32)(uintptr_t)out&&!memcmp(in,out,32)&&term==2&&!submitted);memset(frozen,0x7e,32);reset_copy(&c,stage,in,out,32);assert(!x1830_dma_prepare_cached(&c,frozen)&&!memcmp(frozen,stage,32));direct_contract();direct_failure(PREP_NULL);direct_failure(SUBMIT_ERROR);direct_failure(TIMEOUT);direct_failure(STATUS_ERROR);failure(PREP_NULL);failure(SUBMIT_ERROR);failure(TIMEOUT);failure(STATUS_ERROR);puts("gkd DMA helper pass");return 0; }
