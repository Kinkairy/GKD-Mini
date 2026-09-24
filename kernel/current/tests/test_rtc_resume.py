#!/usr/bin/env python3
"""Exercise the actual RTC reader against the observed framebuffer resume marker."""
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
UPSTREAM = Path('/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1')
COMMIT = '91fe78280ac7dd0dae0f58cb271e821bd39ba97e'
IMAGE = 'local/c-builder:2026.08.02-kernel'
IMAGE_ID = 'sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1'
RELATIVE = 'drivers/rtc/rtc-jz4740.c'
PATCHES = ROOT / 'kernel/current/patches'

FIXTURE = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#define ID_X1830 4
#define JZ_REG_RTC_SCRATCHPAD 0x34
#define JZ_REG_RTC_SEC 0x04
#define JZ_REG_RTC_CTRL 0x00
#define JZ_RTC_CTRL_ENABLE 0x01
struct jz4740_rtc { int type; uint32_t marker, seconds; int unstable; };
struct device { struct jz4740_rtc *rtc; };
struct rtc_time { uint32_t seconds; };
static struct jz4740_rtc *dev_get_drvdata(struct device *d) { return d->rtc; }
static uint32_t jz4740_rtc_reg_read(struct jz4740_rtc *r, size_t reg) {
 if (reg == JZ_REG_RTC_SCRATCHPAD) return r->marker;
 if (reg == JZ_REG_RTC_CTRL) return r->unstable == 2 ? 0 : JZ_RTC_CTRL_ENABLE;
 if (reg != JZ_REG_RTC_SEC) return 0;
 return r->unstable ? r->seconds++ : r->seconds;
}
static void rtc_time64_to_tm(uint32_t secs, struct rtc_time *time) { time->seconds=secs; }
/* ACTUAL_DRIVER_FUNCTION */
static int check(int type, uint32_t marker, int unstable, int expected) {
 struct jz4740_rtc rtc={type,marker,1790252365U,unstable};
 struct device dev={&rtc}; struct rtc_time time={0};
 int result=jz4740_rtc_read_time(&dev,&time);
 if (result != expected || (!result && time.seconds != 1790252365U) ||
     (result && time.seconds != 0)) {
  fprintf(stderr,"RTC case failed type=%d marker=%08x result=%d expected=%d\n",type,marker,result,expected);
  return 1;
 }
 return 0;
}
int main(void) {
 int failed=0;
 failed+=check(ID_X1830,0x12345678U,0,0);
 failed+=check(ID_X1830,0x38444b10U,0,0);
 failed+=check(ID_X1830,0x52323914U,0,0);
 failed+=check(ID_X1830,0xd4000005U,0,EXPECT_RESUME_VALID ? 0 : -EINVAL);
 failed+=check(0,0xd4000005U,0,-EINVAL);
 failed+=check(ID_X1830,0xd4000004U,0,EXPECT_RESUME_VALID ? 0 : -EINVAL);
 failed+=check(ID_X1830,0U,0,EXPECT_RESUME_VALID ? 0 : -EINVAL);
 failed+=check(ID_X1830,0xffffffffU,0,EXPECT_RESUME_VALID ? 0 : -EINVAL);
 failed+=check(ID_X1830,0x9fffffffU,0,EXPECT_RESUME_VALID ? 0 : -EINVAL);
 failed+=check(0,0xffffffffU,0,-EINVAL);
 if (EXPECT_RESUME_VALID) failed+=check(ID_X1830,0x12345678U,2,-EINVAL);
 failed+=check(0,0x12345678U,0,0);
 failed+=check(0,0x38444b10U,0,-EINVAL);
 failed+=check(ID_X1830,0x12345678U,1,-EIO);
 if (EXPECT_RESUME_VALID) failed+=check(ID_X1830,0xd4000005U,1,-EIO);
 if (failed) return 1;
 puts(EXPECT_RESUME_VALID ? "RTC_RESUME_FIX=PASS" : "RTC_RESUME_REGRESSION=REPRODUCED");
 return 0;
}
"""

def reader(text):
 start = text.index('static int jz4740_rtc_read_time(')
 end = text.index('static int jz4740_rtc_set_time(', start)
 return text[start:end]

def main():
 image_id = subprocess.check_output(['docker','image','inspect',IMAGE,'--format','{{.Id}}'],text=True).strip()
 if image_id != IMAGE_ID:
  raise RuntimeError('builder image mismatch')
 base = PATCHES / '0001-rc34-accepted-kernel.patch'
 if hashlib.sha256(base.read_bytes()).hexdigest() != '9740d17d106046163fd3e1aca0a32b1b02a85b29a44ea14392670bcf199d8045':
  raise RuntimeError('accepted base patch mismatch')
 with tempfile.TemporaryDirectory(prefix='gkd-rtc-regression-',dir='/tmp/gkd-mini-public') as tmp:
  work = Path(tmp); driver = work / RELATIVE; driver.parent.mkdir(parents=True)
  driver.write_bytes(subprocess.check_output(['git','show',COMMIT+':'+RELATIVE],cwd=UPSTREAM))
  subprocess.run(['git','apply','--include='+RELATIVE,str(base)],cwd=work,check=True)
  before = driver.read_text()
  subprocess.run(['git','apply',str(PATCHES/'0010-rtc-resume-marker.patch')],cwd=work,check=True)
  after = driver.read_text()
  for name,text in [('before',before),('after',after)]:
   (work/(name+'.c')).write_text(FIXTURE.replace('/* ACTUAL_DRIVER_FUNCTION */',reader(text)))
  script = 'set -eu; for opt in normal sanitized; do flags=""; if [ "$opt" = sanitized ]; then flags="-fsanitize=address,undefined"; fi; cc -std=c99 -Wall -Wextra -Werror -O2 $flags -DEXPECT_RESUME_VALID=0 /case/before.c -o /tmp/rtc-before; /tmp/rtc-before; cc -std=c99 -Wall -Wextra -Werror -O2 $flags -DEXPECT_RESUME_VALID=1 /case/after.c -o /tmp/rtc-after; /tmp/rtc-after; done'
  subprocess.run(['docker','run','--rm','--network','none','--user',str(__import__('os').getuid())+':'+str(__import__('os').getgid()),'-v',str(work)+':/case:ro',IMAGE,'sh','-c',script],check=True)
 print('GKD_RTC_RESUME_TEST=PASS boot_and_resume_markers_disabled_counter_unstable_read_other_models')

if __name__ == '__main__':
 main()
