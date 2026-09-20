/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#define GKD_APP_CONFIG_RUNTIME "/tmp/gkd-settings-save/runtime"
#define GKD_APP_CONFIG_EMBEDDED "/tmp/gkd-settings-save/embedded.conf"
#define GKD_APP_CONFIG_P2 "/tmp/gkd-settings-save/device"
#define GKD_APP_CONFIG_EXEC "/bin/false"
#define GKD_APP_CONFIG_GUARD "/bin/false"
#define GKD_APP_CONFIG_CORE_RUN "/tmp/gkd-settings-save/core-run"
#define GKD_APP_CONFIG_CORE_STATE "/tmp/gkd-settings-save/core-state"
#define main gkd_config_store_main
#include "../source/gkd-app-config-store.c"
#undef main
#include <assert.h>

static const char generation_a[]=
 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char generation_b[]=
 "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char generation_c[]=
 "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
static const char old_override[]=
 "volume_step=7\nui_language=zh\nui_dynamic_effects=enabled\n";
static char wanted_override[256];
static dev_t expected_device;
static int calls,mode;

int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *name,struct stat *st)
{
 if(strcmp(name,GKD_APP_CONFIG_P2))return __real_stat(name,st);
 memset(st,0,sizeof(*st));st->st_mode=S_IFBLK|0600;st->st_rdev=expected_device;return 0;
}
static void make_dir(const char *name)
{assert(!mkdir(name,0700));}
static void write_file(const char *name,const char *text)
{
 int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);assert(fd>=0);
 size_t left=strlen(text);const char *cursor=text;
 while(left){ssize_t n=write(fd,cursor,left);assert(n>0);cursor+=n;left-=(size_t)n;}
 assert(!close(fd));
}
static void set_wanted(unsigned sleep,unsigned style)
{
 assert(snprintf(wanted_override,sizeof(wanted_override),
  "volume_step=7\nui_dynamic_effects=disabled\n"
  "auto_suspend_timeout_seconds=%u\nui_show_fps=enabled\nui_language=en\ninput_style=%s\n",
  sleep*60U,gkd_input_style_name(style))>0);
}
static char *read_file(const char *name)
{
 static char data[4096];int fd=open(name,O_RDONLY|O_CLOEXEC);assert(fd>=0);
 ssize_t n=read(fd,data,sizeof(data)-1U);assert(n>=0);data[n]=0;assert(!close(fd));return data;
}
static void generation_dir(const char *root,const char *generation)
{
 char path[PATH_MAX];
 assert(snprintf(path,sizeof(path),"%s/generations/%s",root,generation)<(int)sizeof(path));
 if(mkdir(path,0555))assert(errno==EEXIST);
 assert(snprintf(path,sizeof(path),"%s/generations/%s/generation",root,generation)<(int)sizeof(path));
 char line[66];assert(snprintf(line,sizeof(line),"%s\n",generation)==65);write_file(path,line);
 assert(!chmod(path,0444));
}
static void switch_generation(const char *root,const char *generation)
{
 char current[PATH_MAX],temporary[PATH_MAX],target[96];
 generation_dir(root,generation);
 assert(snprintf(current,sizeof(current),"%s/current",root)<(int)sizeof(current));
 assert(snprintf(temporary,sizeof(temporary),"%s/.fixture-current",root)<(int)sizeof(temporary));
 assert(snprintf(target,sizeof(target),"generations/%s",generation)<(int)sizeof(target));
 assert(!unlink(temporary)||errno==ENOENT);assert(!symlink(target,temporary));
 assert(!rename(temporary,current));
}
static void setup(void)
{
 assert(system("rm -rf -- /tmp/gkd-settings-save")==0);
 make_dir("/tmp/gkd-settings-save");
 make_dir(GKD_APP_CONFIG_RUNTIME);make_dir(GKD_APP_CONFIG_CORE_RUN);
 make_dir(GKD_APP_CONFIG_CORE_STATE);make_dir(GKD_APP_CONFIG_P2);
 make_dir(GKD_APP_CONFIG_CORE_RUN "/generations");
 make_dir(GKD_APP_CONFIG_CORE_STATE "/generations");
 make_dir(GKD_APP_CONFIG_P2 "/gkd-mini");
 struct stat st;int fd=open(GKD_APP_CONFIG_P2,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
 assert(fd>=0&&!fstat(fd,&st)&&!close(fd));expected_device=st.st_dev;
 write_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf",old_override);
 write_file(GKD_APP_CONFIG_P2 "/gkd-mini/gdkmini.override.conf",old_override);
 set_wanted(5U,0);
 switch_generation(GKD_APP_CONFIG_CORE_RUN,generation_a);
 switch_generation(GKD_APP_CONFIG_CORE_STATE,generation_a);
 calls=0;mode=0;
}
static int fake_command(const char *verb,const char *argument,const char *override,
                        char *output,size_t size,void *unused)
{
 (void)unused;calls++;
 if(!strcmp(verb,"validate")){
  assert(!argument&&!strcmp(override,GKD_APP_CONFIG_RUNTIME "/config.override.conf"));
  snprintf(output,size,"GKD_CONFIG_VALID sha256=%s\n",generation_a);return 0;
 }
 if(!strcmp(verb,"prepare")){
  assert(!argument&&strcmp(override,GKD_APP_CONFIG_RUNTIME "/config.override.conf"));
  assert(!strcmp(read_file(override),wanted_override));
  if(mode==1){errno=EINVAL;return -1;}
  if(mode==5){
   switch_generation(GKD_APP_CONFIG_CORE_RUN,generation_c);
   switch_generation(GKD_APP_CONFIG_CORE_STATE,generation_c);
  }
  snprintf(output,size,"GKD_CONFIG_%s generation=%s%s\n",mode==4?"UNCHANGED":"PREPARED",
           mode==4?generation_a:generation_b,mode==4?"":" changed=4");return 0;
 }
 assert(!strcmp(verb,"commit")&&argument);
 if(!strcmp(argument,generation_b)&&(mode==2||mode==3)){
  switch_generation(GKD_APP_CONFIG_CORE_RUN,generation_b);errno=EIO;return -1;
 }
 if(!strcmp(argument,generation_a)&&mode==3){errno=EIO;return -1;}
 switch_generation(GKD_APP_CONFIG_CORE_RUN,argument);
 switch_generation(GKD_APP_CONFIG_CORE_STATE,argument);
 snprintf(output,size,"GKD_CONFIG_COMMITTED generation=%s changed=4\n",argument);return 0;
}
static int etc_fd(void)
{int fd=open(GKD_APP_CONFIG_P2,O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(fd>=0);return fd;}
static void assert_old(void)
{
 assert(!strcmp(read_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf"),old_override));
 assert(!strcmp(read_file(GKD_APP_CONFIG_P2 "/gkd-mini/gdkmini.override.conf"),old_override));
 assert(!both_generations(generation_a));
}
static void success(void)
{
 static const unsigned valid_sleep[]={0U,5U,10U,15U,30U,60U};
 for(unsigned style=0;style<3;style++)for(size_t i=0;i<sizeof(valid_sleep)/sizeof(valid_sleep[0]);++i){
  setup();set_wanted(valid_sleep[i],style);int etc=etc_fd();char generation[65];
  enum settings_result saved=settings_save(etc,generation_a,0,valid_sleep[i],1,0,
                                            style,fake_command,NULL,generation);
  if(saved!=SETTINGS_SAVED)fprintf(stderr,"success failed result=%d errno=%d calls=%d sleep=%u\n",
                                   saved,errno,calls,valid_sleep[i]);
  assert(saved==SETTINGS_SAVED);
  assert(!strcmp(generation,generation_b)&&calls==3&&!both_generations(generation_b));
  assert(!strcmp(read_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf"),wanted_override));
  assert(!strcmp(read_file(GKD_APP_CONFIG_P2 "/gkd-mini/gdkmini.override.conf"),wanted_override));
  close(etc);
 }
}
static void rejected_without_calls(void)
{
 setup();int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,3,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==0);assert_old();
 static const unsigned invalid_sleep[]={1U,11U,59U};
 assert(settings_save(etc,generation_c,0,5,1,0,0,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==0);assert_old();
 assert(settings_save(etc,generation_a,2,5,1,0,0,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==0);assert_old();
 for(size_t i=0;i<sizeof(invalid_sleep)/sizeof(invalid_sleep[0]);++i){
  char sleep[3];assert(snprintf(sleep,sizeof(sleep),"%u",invalid_sleep[i])>0);
  assert(settings_save(etc,generation_a,0,invalid_sleep[i],1,0,
                       0,fake_command,NULL,generation)==SETTINGS_FAILED);
  assert(calls==0);assert_old();
  char *invalid_argv[]={"gkd-app-config-store","settings-save","3",(char *)generation_a,
                        "0",sleep,"1","0","0",NULL};
  assert(gkd_config_store_main(9,invalid_argv)==1&&calls==0);assert_old();
 }
 char *argv[]={"gkd-app-config-store","settings-cancel",NULL};
 assert(gkd_config_store_main(2,argv)==2&&calls==0);assert_old();close(etc);
}
static void prepare_failure(void)
{
 setup();mode=1;int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,0,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==2);assert_old();close(etc);
}
static void rollback(int unknown)
{
 setup();mode=unknown?3:2;int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,0,fake_command,NULL,generation)==
        (unknown?SETTINGS_UNKNOWN:SETTINGS_FAILED));
 assert(calls==4);
 assert(!strcmp(read_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf"),old_override));
 assert(!strcmp(read_file(GKD_APP_CONFIG_P2 "/gkd-mini/gdkmini.override.conf"),old_override));
 if(unknown)assert(both_generations(generation_a));else assert(!both_generations(generation_a));
 close(etc);
}
static void concurrent_change(void)
{
 setup();mode=5;int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,0,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==2&&!both_generations(generation_c));
 assert(!strcmp(read_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf"),old_override));
 assert(!strcmp(read_file(GKD_APP_CONFIG_P2 "/gkd-mini/gdkmini.override.conf"),old_override));
 close(etc);
}
static void compile_lock_busy(void)
{
 setup();assert(!mkdir(GKD_APP_CONFIG_CORE_RUN "/.compile-lock",0700));
 int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,0,fake_command,NULL,generation)==SETTINGS_FAILED);
 assert(calls==2);assert_old();
 assert(!rmdir(GKD_APP_CONFIG_CORE_RUN "/.compile-lock"));close(etc);
}
static void unchanged(void)
{
 setup();mode=4;int etc=etc_fd();char generation[65];
 assert(settings_save(etc,generation_a,0,5,1,0,0,fake_command,NULL,generation)==SETTINGS_SAVED);
 assert(!strcmp(generation,generation_a)&&calls==2&&!both_generations(generation_a));
 assert(!strcmp(read_file(GKD_APP_CONFIG_RUNTIME "/config.override.conf"),wanted_override));
 close(etc);
}
int main(void)
{
 assert(geteuid()==0);success();rejected_without_calls();prepare_failure();rollback(0);rollback(1);
 concurrent_change();compile_lock_busy();unchanged();
 puts("GKD_APP_SETTINGS_SAVE_FIXTURE=PASS sparse/generation/stale/sleep-ladder/cancel-no-call/prepare/rollback/unknown/concurrent/lock/unchanged");
 return 0;
}
