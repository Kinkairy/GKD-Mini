/* The driver logic is included from actual applied codec source.
 * Only regmap, GPIO and ASoC transports are modeled here. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define BIT(x) (1U << (x))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define READ_ONCE(x) (x)
#define test_bit(n,p) (((p)[(n)/(8*sizeof(unsigned long))] >> ((n)%(8*sizeof(unsigned long)))) & 1UL)
#define SNDRV_PCM_FORMAT_S16_LE 2
#define SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK 0xf000
#define SND_SOC_DAIFMT_CBC_CFC 0x4000
#define SND_SOC_DAIFMT_CBP_CFP 0x1000
#define SND_SOC_DAIFMT_FORMAT_MASK 0xf
#define SND_SOC_DAIFMT_I2S 1
#define SND_SOC_DAIFMT_INV_MASK 0xf00
#define SND_SOC_DAIFMT_NB_NF 0
#define REGCACHE_RBTREE 1
typedef uint8_t u8;
struct mutex { int held; };
struct clk { unsigned long rate; };
struct gpio_desc { int value; };
struct reg_default { unsigned reg, def; };
struct regmap_format { size_t val_bytes; };
struct regmap_config {
 unsigned reg_bits, val_bits, max_register;
 const struct reg_default *reg_defaults;
 unsigned num_reg_defaults, cache_type;
 bool use_single_read, use_single_write;
};
struct regmap {
 unsigned h[56], c[56], d[56], por[56];
 bool only, cache_bypass, dirty, no_defaults, use_single_write, raw_capable;
 int writes, fail_write, fail_sync_reg, fail_single_enxio_reg, sync_calls, raw_fail_1a_1b;
 unsigned raw_calls, single_calls, raw_base, raw_len, last_reg, last_len;
 unsigned reg_stride;
 struct regmap_format format;
 void *dev;
};
struct es8323_priv;
struct snd_soc_component { struct es8323_priv *priv; void *dev; struct regmap *map; };
struct snd_soc_dai { struct snd_soc_component *component; };
struct snd_pcm_substream { int unused; };
struct snd_pcm_hw_params { int format; unsigned rate; };
static int errors;
#define dev_err(dev,...) do { (void)(dev); errors++; } while(0)
#define dev_dbg(dev,...) do { (void)(dev); } while(0)
#define snd_soc_component_get_drvdata(c) ((c)->priv)
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held=1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held=0; }
static unsigned long clk_get_rate(struct clk *c) { return c->rate; }
static int params_format(struct snd_pcm_hw_params *p) { return p->format; }
static unsigned params_rate(struct snd_pcm_hw_params *p) { return p->rate; }
static void usleep_range(unsigned a,unsigned b) { (void)a; (void)b; }
static void msleep(unsigned t) { (void)t; }
static int gpiod_get_value_cansleep(struct gpio_desc *g) { return g->value; }
static void gpiod_set_value_cansleep(struct gpio_desc *g,int value) { g->value=value; }
static int regmap_write(struct regmap *m,unsigned r,unsigned v) {
 assert(r<56);
 if(!m->cache_bypass) m->c[r]=v;
 if(m->only) { assert(!m->cache_bypass); m->dirty=true; return 0; }
 if(++m->writes==m->fail_write) return -EIO;
 if(r==0 && v==0x80) memcpy(m->h,m->por,sizeof(m->h));
 m->h[r]=v; return 0;
}
static int snd_soc_component_write(struct snd_soc_component *c,unsigned r,unsigned v) {
 return regmap_write(c->map,r,v);
}
static int snd_soc_component_update_bits(struct snd_soc_component *c,unsigned r,unsigned mask,unsigned v) {
 unsigned next=(c->map->c[r]&~mask)|(v&mask);
 return next==c->map->c[r] ? 0 : regmap_write(c->map,r,next);
}
static void regcache_cache_only(struct regmap *m,bool v) { assert(!v||!m->cache_bypass); m->only=v; }
static void regcache_cache_bypass(struct regmap *m,bool v) { assert(!v||!m->only); m->cache_bypass=v; }
static void regcache_mark_dirty(struct regmap *m) { m->dirty=true; m->no_defaults=true; }
static bool regmap_writeable(struct regmap *m,unsigned r) { (void)m; return r<56; }
static unsigned regcache_get_val(struct regmap *m,const void *block,unsigned i) {
 (void)m; return ((const u8 *)block)[i];
}
static const void *regcache_get_val_addr(struct regmap *m,const void *block,unsigned i) {
 (void)m; return ((const u8 *)block)+i;
}
static bool regcache_reg_needs_sync(struct regmap *m,unsigned r,unsigned v) {
 return !m->no_defaults || v!=m->d[r];
}
static bool regmap_can_raw_write(struct regmap *m) { return m->raw_capable; }
static int _regmap_write(struct regmap *m,unsigned r,unsigned v) {
 m->single_calls++;m->last_reg=r;m->last_len=1;
 if((int)r==m->fail_single_enxio_reg) return -ENXIO;
 if((int)r==m->fail_sync_reg) return -EIO;
 m->h[r]=v;return 0;
}
static int _regmap_raw_write(struct regmap *m,unsigned r,const void *val,size_t len,bool noinc) {
 const u8 *bytes=val;(void)noinc;
 m->raw_calls++;m->raw_base=r;m->raw_len=len/m->format.val_bytes;
 if(m->raw_fail_1a_1b && r<=0x1a && r+m->raw_len>0x1b) return -ENXIO;
 for(unsigned i=0;i<m->raw_len;i++) {
  if((int)(r+i)==m->fail_sync_reg) return -EIO;
  m->h[r+i]=bytes[i];
 }
 return 0;
}
static int regcache_sync(struct regmap *m);
static void es8323_round46_hw_readback(struct snd_soc_component *c);
#include "codec-under-test.h"
#include "es8323-config-under-test.h"
#include "regcache-under-test.h"
static int regcache_sync(struct regmap *m) {
 assert(!m->only && !m->cache_bypass); m->sync_calls++;
 int ret=0;u8 block[56];
 for(unsigned r=0;r<56;r++) block[r]=m->c[r];
 if(m->dirty) ret=regcache_sync_block(m,block,NULL,0,0,56);
 if(!ret) m->dirty=false;
 m->no_defaults=false; return ret;
}
static void es8323_round46_hw_readback(struct snd_soc_component *c) { (void)c; }
struct rig {
 struct regmap map;
 struct clk clock;
 struct gpio_desc amp, hp;
 struct es8323_priv codec;
 struct snd_soc_component component;
 struct snd_soc_dai dai;
};
static void setup(struct rig *r) {
 memset(r,0,sizeof(*r));
 r->map.fail_sync_reg=r->map.fail_single_enxio_reg=-1;
 r->map.reg_stride=1;r->map.format.val_bytes=1;r->map.raw_capable=true;
 r->map.use_single_write=es8323_regmap_config.use_single_write;
 for(unsigned i=0;i<ARRAY_SIZE(test_por_defaults);i++)
  r->map.por[test_por_defaults[i].reg]=test_por_defaults[i].def;
 memcpy(r->map.h,r->map.por,sizeof(r->map.h));
 for(unsigned i=0;i<ARRAY_SIZE(es8323_reg_defaults);i++)
  r->map.d[es8323_reg_defaults[i].reg]=r->map.c[es8323_reg_defaults[i].reg]=es8323_reg_defaults[i].def;
 r->clock.rate=24000000;
 r->codec.regmap=&r->map; r->codec.mclk=&r->clock;
 r->codec.speaker_enable=&r->amp; r->codec.headphone_detect=&r->hp;
 r->component.priv=&r->codec; r->component.map=&r->map;
 r->dai.component=&r->component;
 assert(es8323_component_probe(&r->component)==0);
#if PATCHED
 for(unsigned i=0;i<56;i++) {
  assert(r->map.h[i]==r->map.d[i]);
  assert(r->map.c[i]==r->map.d[i]);
 }
#endif
 assert(es8323_set_fmt(&r->dai,SND_SOC_DAIFMT_CBP_CFP|SND_SOC_DAIFMT_I2S)==0);
}
static void configure(struct rig *r,bool params) {
 if(params) {
  struct snd_pcm_hw_params p={SNDRV_PCM_FORMAT_S16_LE,44100};
  assert(es8323_hw_params(NULL,&p,&r->dai)==0);
 }
 assert(r->map.h[8]==(params?0xc0:0x80));
 assert(es8323_mute_stream(&r->dai,0,0)==0);
 assert(regmap_write(&r->map,0x1a,33)==0);
 assert(regmap_write(&r->map,0x1b,71)==0);
 assert(regmap_write(&r->map,0x30,17)==0);
 assert(regmap_write(&r->map,0x31,23)==0);
}
#if PATCHED
#if !EXPECT_SINGLE_WRITE
static void reproduce_r12_bulk_sync(void) {
 struct rig r;setup(&r);configure(&r,true);
 u8 pair[2]={33,71};
 assert(!r.map.use_single_write);
 r.map.raw_fail_1a_1b=1;
 assert(regcache_sync_block(&r.map,pair,NULL,0x1a,0,2)==-ENXIO);
 assert(r.map.raw_calls==1 && r.map.raw_base==0x1a && r.map.raw_len==2);
 r.map.raw_calls=0;
 assert(es8323_component_suspend(&r.component)==0);
 assert(es8323_component_resume(&r.component)==-ENXIO);
 assert(r.map.raw_calls);
 assert(!r.map.single_calls && r.codec.resume_failed && !r.amp.value);
}
#endif
static void cycle(bool params) {
 struct rig r;setup(&r);configure(&r,params);
#if EXPECT_SINGLE_WRITE
 assert(r.map.use_single_write);
 r.map.raw_fail_1a_1b=1;
#endif
 for(int n=0;n<3;n++) {
  unsigned desired[56];memcpy(desired,r.map.c,sizeof(desired));
  assert(es8323_component_suspend(&r.component)==0);
  assert(!r.amp.value && r.map.only && r.map.dirty && !r.map.cache_bypass);
  assert(!memcmp(desired,r.map.c,sizeof(desired)));
  assert(es8323_component_resume(&r.component)==0);
  assert(!r.codec.resume_failed && !r.map.only && !r.map.cache_bypass && !r.map.dirty);
  assert(!memcmp(desired,r.map.c,sizeof(desired)));
  assert(!memcmp(desired,r.map.h,sizeof(desired)));
  assert(!r.map.raw_calls && r.map.single_calls);
  assert(!r.codec.lock.held);
 }
}
static void failure(int write_offset,int sync_reg) {
 struct rig r;setup(&r);configure(&r,true);
 unsigned desired[56];memcpy(desired,r.map.c,sizeof(desired));
 assert(es8323_component_suspend(&r.component)==0);
 r.map.fail_write=write_offset?r.map.writes+write_offset:0;
 r.map.fail_sync_reg=sync_reg;int before_errors=errors;
 assert(es8323_component_resume(&r.component)==-EIO);
 assert(errors>=before_errors+1 && r.codec.resume_failed);
 assert(!r.amp.value && !r.map.only && !r.map.cache_bypass && !r.codec.lock.held);
 assert(r.map.h[0x19]==6 && !memcmp(desired,r.map.c,sizeof(desired)));
 assert(es8323_mute_stream(&r.dai,0,0)==-EIO);
 struct snd_pcm_hw_params p={SNDRV_PCM_FORMAT_S16_LE,44100};
 assert(es8323_hw_params(NULL,&p,&r.dai)==-EIO);
 /* The actual headphone IRQ helper must not bypass the failure gate. */
 es8323_update_speaker(&r.codec);assert(!r.amp.value);
 r.hp.value=1;es8323_update_speaker(&r.codec);assert(!r.amp.value);
 r.map.fail_write=0;r.map.fail_sync_reg=-1;
 assert(es8323_component_resume(&r.component)==0);
 assert(!r.codec.resume_failed && !memcmp(desired,r.map.h,sizeof(desired)));
 r.hp.value=0;es8323_update_speaker(&r.codec);assert(r.amp.value);
 assert(es8323_mute_stream(&r.dai,0,0)==0);
}
static void single_enxio_failure(void) {
 struct rig r;setup(&r);configure(&r,true);
 unsigned desired[56];memcpy(desired,r.map.c,sizeof(desired));
 assert(es8323_component_suspend(&r.component)==0);
 r.map.raw_fail_1a_1b=1;r.map.fail_single_enxio_reg=0x1b;
 assert(es8323_component_resume(&r.component)==-ENXIO);
 assert(!r.map.raw_calls && r.map.last_reg==0x1b && r.codec.resume_failed && !r.amp.value);
 assert(es8323_mute_stream(&r.dai,0,0)==-EIO);
 r.map.fail_single_enxio_reg=-1;
 assert(es8323_component_resume(&r.component)==0);
 assert(!r.codec.resume_failed && !r.map.raw_calls && !memcmp(desired,r.map.h,sizeof(desired)));
}
#endif
int main(void) {
#if PATCHED
#if EXPECT_SINGLE_WRITE
 cycle(false);cycle(true);
 for(int n=1;n<=50;n++) failure(n,-1);
 failure(0,8);failure(0,0x30);failure(0,0x31);
 single_enxio_failure();
 puts("GKD_AUDIO_ACTUAL_FUNCTIONS=PASS regcache-single defaults56 cold/master80/masterc0 repeated6 init-faults50 partial-sync3 single-1b-ENXIO headphone/unmute/hwparams-gates recovery");
#else
 reproduce_r12_bulk_sync();
 puts("GKD_AUDIO_R12_REGCACHE_BULK=REPRODUCED actual_sync raw=1a-1b errno=ENXIO");
#endif
#else
 struct rig r;setup(&r);configure(&r,true);
 unsigned desired=r.map.c[8];assert(desired==0xc0);
 assert(es8323_component_suspend(&r.component)==0);
 assert(es8323_component_resume(&r.component)==0);
 assert(r.map.h[8]==0 && r.map.c[8]==0);
 puts("GKD_AUDIO_OLD_BUG=REPRODUCED actual_resume lost-master=c0-to-00");
#endif
 return 0;
}
