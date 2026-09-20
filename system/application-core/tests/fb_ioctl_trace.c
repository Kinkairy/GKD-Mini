/* SPDX-License-Identifier: GPL-2.0 */
/* Temporary read-only ptrace observer for the approved MIPS device diagnosis.
 * Reads framebuffer ioctl arguments/results; never changes registers or memory.
 * Seize semantics preserve application signals, detach every traced task. */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
struct task {pid_t pid;unsigned request,address;int live,interrupt;};
static struct task tasks[256];
static volatile sig_atomic_t stopping;
static void stop(int sig){(void)sig;stopping=1;}
static struct task *lookup(pid_t pid)
{
 for(unsigned i=0;i<256;i++)if(tasks[i].live&&tasks[i].pid==pid)return &tasks[i];
 for(unsigned i=0;i<256;i++)if(!tasks[i].live){tasks[i]=(struct task){.pid=pid,.live=1};return &tasks[i];}
 return NULL;
}
static int peek(pid_t pid,unsigned address,void *data,size_t count)
{
 for(size_t i=0;i<count;i+=sizeof(long)){
  errno=0;long word=ptrace(PTRACE_PEEKDATA,pid,(void *)(uintptr_t)(address+i),0);
  if(errno)return -1;size_t n=count-i;if(n>sizeof(word))n=sizeof(word);memcpy((char *)data+i,&word,n);
 }return 0;
}
static void report(struct task *t,const char *phase,uint64_t regs[38])
{
 if(t->request==FBIOGET_FSCREENINFO){
  struct fb_fix_screeninfo f;if(peek(t->pid,t->address,&f,sizeof(f)))return;
  printf("pid=%ld %s cmd=FIX return=%" PRId64 " error=%llu smem=%lu len=%u pitch=%u ypan=%u\n",
   (long)t->pid,phase,(int64_t)regs[2],(unsigned long long)regs[7],f.smem_start,f.smem_len,f.line_length,f.ypanstep);
 }else{
  struct fb_var_screeninfo v;if(peek(t->pid,t->address,&v,sizeof(v)))return;
  printf("pid=%ld %s cmd=%x return=%" PRId64 " error=%llu size=%ux%u virtual=%ux%u offset=%u,%u bpp=%u activate=%u\n",
   (long)t->pid,phase,t->request,(int64_t)regs[2],(unsigned long long)regs[7],
   v.xres,v.yres,v.xres_virtual,v.yres_virtual,v.xoffset,v.yoffset,v.bits_per_pixel,v.activate);
 }fflush(stdout);
}
int main(int argc,char **argv)
{
 if(argc!=2||geteuid())return 2;char *end;long target=strtol(argv[1],&end,10);
 if(*end||target<=1)return 2;
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGALRM,stop);alarm(60);
 unsigned options=PTRACE_O_TRACESYSGOOD|PTRACE_O_TRACEFORK|PTRACE_O_TRACEVFORK|PTRACE_O_TRACECLONE|PTRACE_O_TRACEEXEC;
 if(ptrace(0x4206,(pid_t)target,0,(void *)(uintptr_t)options)||ptrace(0x4207,(pid_t)target,0,0)){perror("seize");return 1;}
 lookup((pid_t)target);
 for(;;){
  unsigned alive=0;
  for(unsigned i=0;i<256;i++)if(tasks[i].live){alive++;if(stopping&&!tasks[i].interrupt){
   tasks[i].interrupt=1;(void)ptrace(0x4207,tasks[i].pid,0,0);
  }}
  if(!alive)break;
  int status;pid_t pid=waitpid(-1,&status,__WALL);
  if(pid<0){if(errno==EINTR)continue;perror("wait");return 1;}
  struct task *t=lookup(pid);if(!t)return 1;
  if(WIFEXITED(status)||WIFSIGNALED(status)){t->live=0;continue;}
  if(!WIFSTOPPED(status))continue;
  int sig=WSTOPSIG(status);unsigned event=(unsigned)status>>16;
  if(stopping){(void)ptrace(PTRACE_DETACH,pid,0,(void *)(uintptr_t)((sig==SIGTRAP||sig==(SIGTRAP|128))?0:sig));t->live=0;continue;}
  if(event==PTRACE_EVENT_FORK||event==PTRACE_EVENT_VFORK||event==PTRACE_EVENT_CLONE){
   unsigned long child=0;(void)ptrace(PTRACE_GETEVENTMSG,pid,0,&child);lookup((pid_t)child);
  }
  if(sig==(SIGTRAP|128)){
   unsigned char info[128]={0};uint64_t regs[38];
   if(ptrace(0x420e,pid,(void *)sizeof(info),info)<0||ptrace(PTRACE_GETREGS,pid,0,regs)<0){perror("regs");stopping=1;continue;}
   if(info[0]==1){
    t->request=0;
    unsigned cmd=(unsigned)regs[5];
    if(regs[2]==4054U&&(cmd==FBIOGET_VSCREENINFO||cmd==FBIOPUT_VSCREENINFO||cmd==FBIOGET_FSCREENINFO||cmd==FBIOPAN_DISPLAY)){
     t->request=cmd;t->address=(unsigned)regs[6];if(cmd==FBIOPUT_VSCREENINFO||cmd==FBIOPAN_DISPLAY)report(t,"IN",regs);
    }
   }else if(info[0]==2&&t->request){report(t,"OUT",regs);t->request=0;}
   sig=0;
  }else if(sig==SIGSEGV){
   uint64_t regs[38];if(!ptrace(PTRACE_GETREGS,pid,0,regs))printf("CRASH pid=%ld pc=%llx bad=%llx a0=%llx a1=%llx a2=%llx\n",(long)pid,(unsigned long long)regs[34],(unsigned long long)regs[35],(unsigned long long)regs[4],(unsigned long long)regs[5],(unsigned long long)regs[6]);
   fflush(stdout);stopping=1;(void)ptrace(PTRACE_DETACH,pid,0,(void *)(uintptr_t)sig);t->live=0;continue;
  }else if(sig==SIGTRAP&&event)sig=0;
  if(ptrace(PTRACE_SYSCALL,pid,0,(void *)(uintptr_t)sig)&&errno!=ESRCH){perror("resume");stopping=1;}
 }
 puts("FB_TRACE_DETACHED");return 0;
}
