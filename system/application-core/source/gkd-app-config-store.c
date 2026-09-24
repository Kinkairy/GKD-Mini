/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-job.h"
#include "gkd-settings-values.h"
#include "gkd-input-style.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef GKD_APP_CONFIG_RUNTIME
#define GKD_APP_CONFIG_RUNTIME "/run/gkd-application"
#define GKD_APP_CONFIG_EMBEDDED "/etc/gkd-mini/application.override.conf"
#define GKD_APP_CONFIG_P2 "/dev/mmcblk0p2"
#define GKD_APP_CONFIG_EXEC "/usr/sbin/gkd-config"
#define GKD_APP_CONFIG_GUARD "/usr/sbin/gkd-app-card-guard"
#endif
#ifndef GKD_APP_CONFIG_CORE_RUN
#define GKD_APP_CONFIG_CORE_RUN "/run/gkd-config"
#endif
#ifndef GKD_APP_CONFIG_CORE_STATE
#define GKD_APP_CONFIG_CORE_STATE "/run/gkd-config-state"
#endif
#define SETTINGS_LIMIT 65536U
struct settings_blob { size_t size; int exists; char data[SETTINGS_LIMIT]; };
enum settings_result { SETTINGS_FAILED=-1, SETTINGS_UNKNOWN=0, SETTINGS_SAVED=1 };
typedef int (*config_command_fn)(const char *,const char *,const char *,char *,size_t,void *);
static uint64_t now_ms(void)
{
 struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;
 return (uint64_t)ts.tv_sec*1000U+(uint64_t)ts.tv_nsec/1000000U;
}
static int run(const char *program,const char *argument)
{
 struct gkd_app_job job=GKD_APP_JOB_INIT;char *argv[]={(char *)program,(char *)argument,NULL};
 if(gkd_app_job_start(&job,argv,30000U,now_ms()))return -1;
 while(job.pid>0){gkd_app_job_poll(&job,now_ms());usleep(10000);}
 int error=job.state==GKD_JOB_DONE?0:job.error?job.error:EIO;
 if(!error&&job.output[0])fputs(job.output,stdout);
 gkd_app_job_close(&job);errno=error;return error?-1:0;
}
static int exact_uint(const char *text,unsigned maximum,unsigned *result)
{
 unsigned value=0;if(!text||!*text)return 0;
 if(text[0]=='0'&&text[1])return 0;
 for(const unsigned char *p=(const unsigned char *)text;*p;p++){
  unsigned digit=(unsigned)(*p-'0');
  if(*p<'0'||*p>'9'||value>maximum/10U||
     (value==maximum/10U&&digit>maximum%10U))return 0;
  value=value*10U+digit;
 }
 *result=value;return 1;
}
static int generation_valid(const char *text)
{
 if(!text||strlen(text)!=64U)return 0;
 for(const unsigned char *p=(const unsigned char *)text;*p;p++)
  if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return 0;
 return 1;
}
static int secure_root_directory(int fd)
{
 struct stat st;
 if(fstat(fd,&st))return -1;
 if(!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0022)){errno=EPERM;return -1;}
 return 0;
}
static int read_optional(int directory,const char *name,struct settings_blob *blob)
{
 struct stat st;char extra;ssize_t length,tail;int fd,saved;
 memset(blob,0,sizeof(*blob));
 fd=openat(directory,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){if(errno==ENOENT)return 0;return -1;}
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1U||
    (st.st_mode&0022)||st.st_size<0||st.st_size>(off_t)SETTINGS_LIMIT){
  saved=errno?errno:EPERM;close(fd);errno=saved;return -1;
 }
 do length=read(fd,blob->data,SETTINGS_LIMIT);while(length<0&&errno==EINTR);
 tail=length>=0?read(fd,&extra,1):-1;saved=errno;close(fd);
 if(length<0||tail<0){errno=saved;return -1;}
 if(tail||length!=st.st_size){errno=tail?EFBIG:ESTALE;return -1;}
 if(memchr(blob->data,0,(size_t)length)){errno=EPROTO;return -1;}
 blob->size=(size_t)length;blob->exists=1;return 0;
}
static int same_blob(const struct settings_blob *left,const struct settings_blob *right)
{
 return left->exists==right->exists&&(!left->exists||
        (left->size==right->size&&!memcmp(left->data,right->data,left->size)));
}
static int atomic_blob(int directory,const char *name,const struct settings_blob *blob)
{
 char temporary[80];struct stat st;int output=-1,rc=-1,saved;
 snprintf(temporary,sizeof(temporary),".gkd-app-settings.%ld",(long)getpid());
 output=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 if(output<0)return -1;
 for(size_t offset=0;offset<blob->size;){
  ssize_t written=write(output,blob->data+offset,blob->size-offset);
  if(written<0&&errno==EINTR)continue;
  if(written<=0){if(!written)errno=EIO;goto done;}offset+=(size_t)written;
 }
 if(fsync(output))goto done;
 if(close(output)){output=-1;goto done;}output=-1;
 if(!fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW)){
  if(!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1U){errno=EPERM;goto done;}
 }else if(errno!=ENOENT)goto done;
 if(renameat(directory,temporary,directory,name)||fsync(directory))goto done;
 rc=0;
done:
 saved=errno;if(output>=0)close(output);if(rc)unlinkat(directory,temporary,0);errno=saved;return rc;
}
static int restore_blob(int directory,const char *name,const struct settings_blob *blob)
{
 struct stat st;
 if(blob->exists)return atomic_blob(directory,name,blob);
 if(fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW))return errno==ENOENT?0:-1;
 if(!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1U){errno=EPERM;return -1;}
 if(unlinkat(directory,name,0)||fsync(directory))return -1;
 return 0;
}
static int generation_at(const char *root,char result[65])
{
 char target[96],data[66],extra;struct stat st;ssize_t length,tail;int dir=-1,fd=-1,saved;
 dir=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(dir<0||secure_root_directory(dir))goto fail;
 if(fstatat(dir,"current",&st,AT_SYMLINK_NOFOLLOW)||!S_ISLNK(st.st_mode)||st.st_uid){errno=EPERM;goto fail;}
 length=readlinkat(dir,"current",target,sizeof(target)-1U);
 if(length<0||(size_t)length>=sizeof(target)){if(length>=0)errno=EOVERFLOW;goto fail;}
 target[length]=0;
 if(strncmp(target,"generations/",12)||!generation_valid(target+12)){errno=EPROTO;goto fail;}
 fd=openat(dir,"current/generation",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&0022)){errno=EPERM;goto fail;}
 do length=read(fd,data,65U);while(length<0&&errno==EINTR);
 tail=length>=0?read(fd,&extra,1):-1;
 if(length!=65||tail||memcmp(data,target+12,64)||data[64]!='\n'){errno=EPROTO;goto fail;}
 memcpy(result,target+12,64);result[64]=0;close(fd);close(dir);return 0;
fail:
 saved=errno?errno:EPERM;if(fd>=0)close(fd);if(dir>=0)close(dir);errno=saved;return -1;
}
static int both_generations(const char *expected)
{
 char runtime[65],state[65];
 return generation_at(GKD_APP_CONFIG_CORE_RUN,runtime)||
        generation_at(GKD_APP_CONFIG_CORE_STATE,state)||
        strcmp(runtime,expected)||strcmp(state,expected)?(errno=ESTALE,-1):0;
}
static int exact_receipt(const char *output,const char *prefix,char generation[65])
{
 size_t prefix_size=strlen(prefix);
 if(strncmp(output,prefix,prefix_size)||strlen(output)<prefix_size+65U)return -1;
 memcpy(generation,output+prefix_size,64);generation[64]=0;
 if(!generation_valid(generation))return -1;
 const char *tail=output+prefix_size+64U;
 if(!strcmp(prefix,"GKD_CONFIG_VALID sha256="))return !strcmp(tail,"\n")?0:-1;
 if(!strcmp(prefix,"GKD_CONFIG_UNCHANGED generation="))return !strcmp(tail,"\n")?0:-1;
 if(strncmp(tail," changed=",9))return -1;
 tail+=9;if(!*tail||*tail=='\n')return -1;
 while(*tail>='0'&&*tail<='9')tail++;
 return !strcmp(tail,"\n")?0:-1;
}
static int config_command_real(const char *verb,const char *argument,const char *override,
                               char *output,size_t size,void *unused)
{
 (void)unused;struct gkd_app_job job=GKD_APP_JOB_INIT;int error;
 char *argv[4]={(char *)GKD_APP_CONFIG_EXEC,(char *)verb,(char *)argument,NULL};
 if(!argument)argv[2]=NULL;
 if(setenv("GKD_CONFIG_OVERRIDE",override,1)||
    gkd_app_job_start_transaction(&job,argv,now_ms(),-1))return -1;
 while(job.pid>0){gkd_app_job_poll(&job,now_ms());usleep(10000);}
 error=job.state==GKD_JOB_DONE?0:job.error?job.error:EIO;
 if(!error){
  size_t length=strlen(job.output);
  if(length>=size)error=EOVERFLOW;
  else memcpy(output,job.output,length+1U);
 }
 gkd_app_job_close(&job);errno=error;return error?-1:0;
}
static int directory_at(int parent,const char *name,int create,dev_t device)
{
 struct stat st;int fd;
 if(create&&mkdirat(parent,name,0700)&&errno!=EEXIST)return -1;
 fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 if(fstat(fd,&st)||st.st_uid||st.st_mode&0022||st.st_dev!=device){
  close(fd);errno=EPERM;return -1;
 }
 return fd;
}
static int pinned_etc(int fd)
{
 struct stat st,block;
 if(fstat(fd,&st)||!S_ISDIR(st.st_mode)||st.st_uid||st.st_mode&0022||
    stat(GKD_APP_CONFIG_P2,&block)||!S_ISBLK(block.st_mode)||st.st_dev!=block.st_rdev){errno=EPERM;return -1;}
 return 0;
}
static int copy_atomic(int input,int directory,const char *name)
{
 char temporary[80],buffer[4096];struct stat st;size_t total=0;int output=-1,rc=-1,saved;
 if(fstat(input,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_mode&0022||
    st.st_size<0||st.st_size>65536){errno=EPERM;return -1;}
 snprintf(temporary,sizeof(temporary),".gkd-app-config.%ld",(long)getpid());
 output=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 if(output<0)return -1;
 for(;;){
  ssize_t n=read(input,buffer,sizeof(buffer));
  if(n<0&&errno==EINTR)continue;
  if(n<0)goto done;
  if(!n)break;
  total+=(size_t)n;if(total>65536U){errno=EFBIG;goto done;}
  if(memchr(buffer,0,(size_t)n)){errno=EPROTO;goto done;}
  for(ssize_t offset=0;offset<n;){
   ssize_t written=write(output,buffer+offset,(size_t)(n-offset));
   if(written<0&&errno==EINTR)continue;
   if(written<=0){if(!written)errno=EIO;goto done;}offset+=written;
  }
 }
 if(total!=(size_t)st.st_size){errno=ESTALE;goto done;}
 if(fsync(output))goto done;
 if(close(output)){output=-1;goto done;}
 output=-1;
 /* Refuse a changed target type; never follow or overwrite a symlink. */
 if(!fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW)){
  if(!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1U){errno=EPERM;goto done;}
 }else if(errno!=ENOENT)goto done;
 if(renameat(directory,temporary,directory,name)||fsync(directory))goto done;
 rc=0;
done:
 saved=errno;if(output>=0)close(output);if(rc)unlinkat(directory,temporary,0);errno=saved;return rc;
}
/* Loading at boot/restart is distinct from committing a live configuration.
 * All writes of settings use settings_commit below, never this loader. */
static int load_override(int etc)
{
 struct stat st;int config=-1,runtime=-1,input=-1,rc=-1,saved;const char *source="override";
 if(pinned_etc(etc)||fstat(etc,&st))return -1;
 config=directory_at(etc,"gkd-mini",0,st.st_dev);
 if(config<0&&errno!=ENOENT)return -1;
 runtime=open(GKD_APP_CONFIG_RUNTIME,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(runtime<0||secure_root_directory(runtime))goto done;
 if(config>=0)input=openat(config,"gdkmini.override.conf",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 else errno=ENOENT;
 if(input<0&&errno==ENOENT){
  input=open(GKD_APP_CONFIG_EMBEDDED,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);source="defaults";
 }
 if(input<0)goto done;
 rc=copy_atomic(input,runtime,"config.override.conf");
 if(!rc)printf("GKD_APP_CONFIG=LOADED source=%s\n",source);
done:
 saved=errno;if(input>=0)close(input);if(config>=0)close(config);if(runtime>=0)close(runtime);errno=saved;return rc;
}
static int embedded_override(struct settings_blob *blob)
{
 char path[PATH_MAX];const char *source=GKD_APP_CONFIG_EMBEDDED;
 if(strlen(source)>=sizeof(path)){errno=ENAMETOOLONG;return -1;}
 strcpy(path,source);char *name=strrchr(path,'/');
 if(!name||name==path||!name[1]){errno=EINVAL;return -1;}
 *name++=0;
 int directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(directory<0)return -1;
 int rc=secure_root_directory(directory);
 if(!rc)rc=read_optional(directory,name,blob);
 if(!rc&&!blob->exists){errno=ENOENT;rc=-1;}
 int saved=errno;close(directory);errno=saved;return rc;
}
static int target_setting(const char *line,size_t size)
{
 static const char *keys[]={"ui_dynamic_effects","auto_suspend_timeout_seconds",
                            "ui_show_fps","ui_language","input_style"};
 for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
  size_t length=strlen(keys[i]);
  if(size>length&&line[length]=='='&&!memcmp(line,keys[i],length))return 1;
 }
 return 0;
}
static int append_text(struct settings_blob *blob,const char *text)
{
 size_t length=strlen(text);
 if(length>SETTINGS_LIMIT-blob->size){errno=EFBIG;return -1;}
 memcpy(blob->data+blob->size,text,length);blob->size+=length;return 0;
}
static int settings_candidate(const struct settings_blob *old,unsigned animation,
                              unsigned sleep,unsigned fps,unsigned lang,unsigned style,
                              struct settings_blob *candidate)
{
 size_t offset=0;char values[192];
 memset(candidate,0,sizeof(*candidate));candidate->exists=1;
 while(offset<old->size){
  const char *line=old->data+offset;
  const char *newline=memchr(line,'\n',old->size-offset);
  size_t length=newline?(size_t)(newline-line)+1U:old->size-offset;
  size_t content=length-(newline?1U:0U);
  if(!target_setting(line,content)){
   if(length>SETTINGS_LIMIT-candidate->size){errno=EFBIG;return -1;}
   memcpy(candidate->data+candidate->size,line,length);candidate->size+=length;
   if(!newline&&append_text(candidate,"\n"))return -1;
  }
  offset+=length;
 }
 int count=snprintf(values,sizeof(values),
  "ui_dynamic_effects=%s\nauto_suspend_timeout_seconds=%u\nui_show_fps=%s\nui_language=%s\ninput_style=%s\n",
  animation?"enabled":"disabled",sleep*60U,fps?"enabled":"disabled",lang?"zh":"en",gkd_input_style_name(style));
 if(count<0||(size_t)count>=sizeof(values)){errno=EOVERFLOW;return -1;}
 return append_text(candidate,values);
}
static int blob_at_matches(int directory,const char *name,const struct settings_blob *expected)
{
 struct settings_blob actual;
 return read_optional(directory,name,&actual)||!same_blob(&actual,expected)?(errno=ESTALE,-1):0;
}
static int persistent_matches(int etc,int config,const struct settings_blob *expected)
{
 struct stat st;
 if(config>=0)return blob_at_matches(config,"gdkmini.override.conf",expected);
 if(expected->exists){errno=ESTALE;return -1;}
 if(!fstatat(etc,"gkd-mini",&st,AT_SYMLINK_NOFOLLOW)){errno=ESTALE;return -1;}
 return errno==ENOENT?0:-1;
}
static int core_lock(int acquire)
{
 static const char path[]=GKD_APP_CONFIG_CORE_RUN "/.compile-lock";
 struct stat st;int fd,saved;
 if(!acquire)return rmdir(path);
 if(mkdir(path,0700))return -1;
 fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){saved=errno;rmdir(path);errno=saved;return -1;}
 if(fstat(fd,&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0077)){
  saved=errno?errno:EPERM;close(fd);rmdir(path);errno=saved;return -1;
 }
 close(fd);return 0;
}
static int restore_settings(int etc,int config,int config_created,int runtime,
                            const struct settings_blob *old_persistent,
                            const struct settings_blob *old_runtime,const char *generation,
                            config_command_fn command,void *opaque)
{
 char output[512],current[65];
 (void)restore_blob(runtime,"config.override.conf",old_runtime);
 if(config>=0)(void)restore_blob(config,"gdkmini.override.conf",old_persistent);
 if(config_created&&!old_persistent->exists){
  (void)unlinkat(etc,"gkd-mini",AT_REMOVEDIR);(void)fsync(etc);
  config=-1;
 }
 if(both_generations(generation)){
  if(!command("commit",generation,GKD_APP_CONFIG_RUNTIME "/config.override.conf",
              output,sizeof(output),opaque))
   (void)exact_receipt(output,"GKD_CONFIG_COMMITTED generation=",current);
 }
 if(both_generations(generation)||blob_at_matches(runtime,"config.override.conf",old_runtime)||
    persistent_matches(etc,config,old_persistent))return -1;
 return 0;
}
struct settings_edit { unsigned animation,sleep,fps,lang,style; };
/* One transaction for menu edits and CLI candidates. The CLI's editable
 * override is a proposal, not the committed effective generation. */
static enum settings_result settings_commit(int etc,const char *expected,
 const struct settings_edit *edit,config_command_fn command,void *opaque,
 char saved_generation[65])
{
 struct settings_blob observed_runtime,old_runtime,old_persistent,candidate;
 struct stat etc_stat;char candidate_name[80]={0},candidate_path[PATH_MAX];
 char baseline_name[80]={0},baseline_path[PATH_MAX];
 char output[512],generation[65];int runtime=-1,config=-1;
 int lock=0,config_created=0,saved=EINVAL,result=SETTINGS_FAILED;
 if(etc<0||!generation_valid(expected)||!command||!saved_generation||
    (edit&&(edit->animation>1U||!gkd_settings_sleep_valid(edit->sleep)||
            edit->fps>1U||edit->lang>1U||edit->style>=GKD_INPUT_STYLE_COUNT))){
  errno=EINVAL;return SETTINGS_FAILED;
 }
 if(pinned_etc(etc)||fstat(etc,&etc_stat))return SETTINGS_FAILED;
 runtime=open(GKD_APP_CONFIG_RUNTIME,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(runtime<0||secure_root_directory(runtime))goto done;
 config=directory_at(etc,"gkd-mini",0,etc_stat.st_dev);
 if(config<0&&errno!=ENOENT)goto done;
 if(read_optional(runtime,"config.override.conf",&observed_runtime))goto done;
 if(config>=0){
  if(read_optional(config,"gdkmini.override.conf",&old_persistent))goto done;
 }else memset(&old_persistent,0,sizeof(old_persistent));
 if(both_generations(expected))goto done;
 if(edit){
  old_runtime=observed_runtime;
  if(old_persistent.exists&&!same_blob(&old_runtime,&old_persistent)){errno=ESTALE;goto done;}
  if(settings_candidate(&old_runtime,edit->animation,edit->sleep,edit->fps,edit->lang,edit->style,&candidate))goto done;
  strcpy(baseline_path,GKD_APP_CONFIG_RUNTIME "/config.override.conf");
 }else{
  if(!observed_runtime.exists){errno=ENOENT;goto done;}
  candidate=observed_runtime;
  if(old_persistent.exists)old_runtime=old_persistent;
  else if(embedded_override(&old_runtime))goto done;
  snprintf(baseline_name,sizeof(baseline_name),".settings-baseline.%ld",(long)getpid());
  if(snprintf(baseline_path,sizeof(baseline_path),GKD_APP_CONFIG_RUNTIME "/%s",baseline_name)>=
     (int)sizeof(baseline_path)){errno=EOVERFLOW;goto done;}
  if(atomic_blob(runtime,baseline_name,&old_runtime))goto done;
 }
 snprintf(candidate_name,sizeof(candidate_name),".settings-candidate.%ld",(long)getpid());
 if(snprintf(candidate_path,sizeof(candidate_path),GKD_APP_CONFIG_RUNTIME "/%s",candidate_name)>=
    (int)sizeof(candidate_path)){errno=EOVERFLOW;goto done;}
 if(atomic_blob(runtime,candidate_name,&candidate))goto done;
 if(command("validate",NULL,baseline_path,output,sizeof(output),opaque)||
    exact_receipt(output,"GKD_CONFIG_VALID sha256=",generation)||strcmp(generation,expected)){
  if(!errno)errno=ESTALE;
  goto done;
 }
 if(command("prepare",NULL,candidate_path,output,sizeof(output),opaque))goto done;
 if(exact_receipt(output,"GKD_CONFIG_PREPARED generation=",generation)&&
    exact_receipt(output,"GKD_CONFIG_UNCHANGED generation=",generation)){
  errno=EPROTO;goto done;
 }
 if(core_lock(1))goto done;
 lock=1;
 if(both_generations(expected)||
    blob_at_matches(runtime,"config.override.conf",&observed_runtime)||
    persistent_matches(etc,config,&old_persistent))goto done;
 if(config<0){
  config=directory_at(etc,"gkd-mini",1,etc_stat.st_dev);
  if(config<0)goto done;
  config_created=1;
 }
 if(atomic_blob(config,"gdkmini.override.conf",&candidate)||
    atomic_blob(runtime,"config.override.conf",&candidate))goto rollback;
 if(strcmp(generation,expected)){
  if(command("commit",generation,GKD_APP_CONFIG_RUNTIME "/config.override.conf",
             output,sizeof(output),opaque)||
     exact_receipt(output,"GKD_CONFIG_COMMITTED generation=",saved_generation)||
     strcmp(saved_generation,generation))goto rollback;
 }else memcpy(saved_generation,generation,65U);
 if(both_generations(generation)||blob_at_matches(runtime,"config.override.conf",&candidate)||
    persistent_matches(etc,config,&candidate))goto rollback;
 if(unlinkat(runtime,candidate_name,0)||fsync(runtime))goto rollback;
 if(baseline_name[0]&&(unlinkat(runtime,baseline_name,0)||fsync(runtime)))goto rollback;
 if(core_lock(0))goto rollback;
 lock=0;
 result=SETTINGS_SAVED;errno=0;goto done;
rollback:
 saved=errno?errno:EIO;
 result=restore_settings(etc,config,config_created,runtime,&old_persistent,&old_runtime,
                         expected,command,opaque)?SETTINGS_UNKNOWN:SETTINGS_FAILED;
 errno=saved;
done:
 saved=result==SETTINGS_SAVED?0:(errno?errno:EINVAL);
 if(candidate_name[0]&&runtime>=0)unlinkat(runtime,candidate_name,0);
 if(baseline_name[0]&&runtime>=0)unlinkat(runtime,baseline_name,0);
 if(lock&&core_lock(0)&&result!=SETTINGS_UNKNOWN)result=SETTINGS_UNKNOWN;
 if(config>=0)close(config);
 if(runtime>=0)close(runtime);
 errno=saved;return result;
}
static enum settings_result settings_save(int etc,const char *expected,
 unsigned animation,unsigned sleep,unsigned fps,unsigned lang,unsigned style,
 config_command_fn command,void *opaque,char saved_generation[65])
{
 const struct settings_edit edit={animation,sleep,fps,lang,style};
 return settings_commit(etc,expected,&edit,command,opaque,saved_generation);
}
static int report_settings(enum settings_result result,const char *generation)
{
 int error=errno?errno:EIO;
 if(result==SETTINGS_SAVED){printf("GKD_APP_SETTINGS=SAVED generation=%s\n",generation);return 0;}
 const char *state=result==SETTINGS_UNKNOWN?"unknown":"recoverable";
 printf("GKD_APP_SETTINGS=FAILED state=%s errno=%d\n",state,error);
 fprintf(stderr,"GKD_APP_SETTINGS_DIAGNOSTIC state=%s errno=%d\n",state,error);
 return 1;
}
static int offline_load(void)
{
 static const char mountpoint[]=GKD_APP_CONFIG_RUNTIME "/config-root";
 struct stat root;int mounted=0,fd=-1,local=-1,etc=-1,rc=-1,saved;
 if(run(GKD_APP_CONFIG_GUARD,"mmcblk0"))return -1;
 if(unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))return -1;
 if(mkdir(mountpoint,0700)&&errno!=EEXIST)return -1;
 if(lstat(mountpoint,&root)||!S_ISDIR(root.st_mode)||root.st_uid||root.st_mode&0077){errno=EPERM;return -1;}
 if(mount(GKD_APP_CONFIG_P2,mountpoint,"ext3",MS_NOATIME|MS_NOSUID|MS_NODEV,NULL))return -1;
 mounted=1;fd=open(mountpoint,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0||fstat(fd,&root))goto done;
 local=directory_at(fd,"local",0,root.st_dev);if(local<0)goto done;
 etc=directory_at(local,"etc",0,root.st_dev);if(etc<0)goto done;
 rc=load_override(etc);
done:
 saved=errno;if(etc>=0)close(etc);if(local>=0)close(local);if(fd>=0)close(fd);
 if(mounted&&umount(mountpoint)){rc=-1;saved=errno;}
 if(rmdir(mountpoint)&&!rc){rc=-1;saved=errno;}
 errno=saved;
 if(!rc)rc=run(GKD_APP_CONFIG_EXEC,"apply");
 return rc;
}
int main(int argc,char **argv)
{
 int rc=-1;
 if(geteuid())return 2;
 if(setenv("GKD_CONFIG_OVERRIDE",GKD_APP_CONFIG_RUNTIME "/config.override.conf",1))return 1;
 if(argc==2&&!strcmp(argv[1],"offline-load"))rc=offline_load();
 else if(argc==9&&!strcmp(argv[1],"settings-save")&&!strcmp(argv[2],"3")){
  unsigned animation,sleep,fps,lang,style;char generation[65]={0};
  if(!exact_uint(argv[4],1U,&animation)||!exact_uint(argv[5],60U,&sleep)||
     !gkd_settings_sleep_valid(sleep)||!exact_uint(argv[6],1U,&fps)||
     !exact_uint(argv[7],1U,&lang)||!exact_uint(argv[8],2U,&style)){
   errno=EINVAL;return report_settings(SETTINGS_FAILED,generation);
  }
  enum settings_result saved=settings_save(3,argv[3],animation,sleep,fps,lang,style,
                                           config_command_real,NULL,generation);
  return report_settings(saved,generation);
 }else if(argc==4&&!strcmp(argv[1],"save")&&!strcmp(argv[2],"3")){
  char generation[65]={0};
  enum settings_result saved=settings_commit(3,argv[3],NULL,config_command_real,NULL,generation);
  return report_settings(saved,generation);
 }else if(argc==3&&!strcmp(argv[1],"load")&&!strcmp(argv[2],"3"))rc=load_override(3);
 else{
  fputs("usage: gkd-app-config-store offline-load|load 3|save 3 EXPECTED_GEN|settings-save 3 EXPECTED_GEN ANIMATION SLEEP_MIN FPS LANG STYLE\n",stderr);
  return 2;
 }
 if(rc)fprintf(stderr,"GKD_APP_CONFIG=FAILED errno=%d\n",errno);
 return rc?1:0;
}
