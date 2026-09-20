/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-menu-config.h"
#include "gkd-input-keys.h"
#include "gkd-input-style.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define REQUIRED 15U
static int hash_valid(const char *s)
{
    if(!s||strlen(s)!=64U)return 0;
    for(unsigned i=0;i<64U;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static int id_valid(const char *s)
{
    if(!*s||strlen(s)>=64U)return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'))return 0;
    return 1;
}
static int entry_valid(const char *s)
{
    /* Exec identity is argv[0], not a shell expression; relative OPK paths only. */
    if(!s||!*s||*s=='/'||strlen(s)>=256U)return 0;
    const char *component=s;
    for(const char *p=s;;p++){
        if(*p&&(unsigned char)*p<33U)return 0;
        if(*p=='\\')return 0;
        if(!*p||*p=='/'){
            size_t n=(size_t)(p-component);
            if(!n||(n==1U&&*component=='.')||(n==2U&&!memcmp(component,"..",2)))return 0;
            if(!*p)break;
            component=p+1;
        }
    }
    return 1;
}
static char *trim(char *s)
{
    while(*s==' '||*s=='\t')s++;
    size_t n=strlen(s);
    while(n&&(s[n-1]==' '||s[n-1]=='\t'))s[--n]=0;
    return s;
}
static int number(const char *s,unsigned max,unsigned *out)
{
    unsigned n=0;if(!*s)return -1;
    for(;*s;s++){
        if(*s<'0'||*s>'9'||n>(max-(unsigned)(*s-'0'))/10U)return -1;
        n=n*10U+(unsigned)(*s-'0');
    }
    if(n>max)return -1;
    *out=n;return 0;
}
static int keys(char *s,struct gkd_menu_profile *p)
{
    for(;;){
        char *next=strchr(s,'+');if(next)*next++=0;
        unsigned short code;unsigned raw;
        s=trim(s);
        if(gkd_input_key_code(s,&code)){
            if(number(s,KEY_MAX,&raw)||!raw)return -1;
            code=(unsigned short)raw;
        }
        /* Button codes are not keyboard keys; extended KEY_* codes are valid. */
        if(code==KEY_RESERVED||(code>=BTN_MISC&&code<KEY_OK)||p->key_count==GKD_MENU_CHORD_MAX)return -1;
        for(unsigned i=0;i<p->key_count;i++)if(p->keys[i]==code)return -1;
        p->keys[p->key_count++]=code;
        if(!next)return 0;
        s=next;
    }
}
static int rom_valid(const char *s)
{
 if(!s||strncmp(s,"/media/sdcard/",14)||!s[14]||strlen(s)>=512U) return 0;
 const char *start=s+1;
 for(const char *p=start;;p++) {
  if(*p && ((unsigned char)*p<32U || *p=='\\'))return 0;
  if(!*p||*p=='/') {
   size_t n=(size_t)(p-start);
   if(!n||(n==1&&*start=='.')||(n==2&&!memcmp(start,"..",2)))return 0;
   if(!*p)break;
   start=p+1;
  }
 }
 return 1;
}
static int source_key(const char *text,unsigned short *code)
{
 static const struct {const char *name;unsigned short code;} names[]={
  {"side_dot",KEY_LEFTCTRL},{"side_double_dot",KEY_LEFTALT},
  {"a",KEY_LEFTCTRL},{"b",KEY_LEFTALT},{"x",KEY_LEFTSHIFT},{"y",KEY_SPACE},
  {"up",KEY_UP},{"down",KEY_DOWN},{"left",KEY_LEFT},{"right",KEY_RIGHT},
  {"start",KEY_ENTER},{"select",KEY_ESC},{"l1",KEY_TAB},{"r1",KEY_BACKSPACE},
  {"l2",KEY_PAGEUP},{"r2",KEY_PAGEDOWN}
 };
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
  if(!strcmp(text,names[i].name)){*code=names[i].code;return 0;}
 return -1;
}
static int mapping(const char *name,char *value,struct gkd_menu_profile *p)
{
 unsigned short source;struct gkd_menu_profile target={0};
 if(source_key(name,&source)||p->map_count==GKD_INPUT_ROUTE_MAPS)return -1;
 for(unsigned i=0;i<p->map_count;i++)if(p->maps[i].source==source)return -1;
 if(strcmp(value,"disabled")&&(keys(value,&target)||target.key_count!=1))return -1;
 p->maps[p->map_count++]=(struct gkd_input_route_map){source,target.keys[0]};
 return 0;
}
static int complete(const struct gkd_menu_config *c,unsigned seen)
{
    if(!c->count)return -1;
    const struct gkd_menu_profile *p=&c->profiles[c->count-1U];
    if((seen&REQUIRED)!=REQUIRED)return -1;
    if(p->action==GKD_MENU_CHORD){
        if(!(seen&16U)||!p->key_count||p->hold_ms<20U||p->hold_ms>500U)return -1;
    }else if(seen&(16U|32U))return -1;
    for(unsigned i=0;i+1U<c->count;i++){
        const struct gkd_menu_profile *other=&c->profiles[i];
        if(!strcmp(other->id,p->id)||(!strcmp(other->opk_sha256,p->opk_sha256)&&
           !strcmp(other->desktop,p->desktop)&&!strcmp(other->executable,p->executable)&&!strcmp(other->rom,p->rom)))return -1;
    }
    return 0;
}
int gkd_menu_config_parse(const char *data,size_t bytes,struct gkd_menu_config *out,unsigned *bad_line)
{
    struct gkd_menu_config *c=NULL;char *copy=NULL;int result=-1;unsigned line=0,seen=0,version=0;
    if(bad_line)*bad_line=0;
    if(!data||!out||!bytes||bytes>GKD_MENU_CONFIG_BYTES){errno=EINVAL;return -1;}
    if(memchr(data,0,bytes)){errno=EPROTO;return -1;}
    c=calloc(1,sizeof(*c));copy=malloc(bytes+1U);
    if(!c||!copy){errno=ENOMEM;goto done;}
    memcpy(copy,data,bytes);copy[bytes]=0;
    for(char *cursor=copy;cursor;){
        char *next=strchr(cursor,'\n');if(next)*next++=0;
        ++line;
        size_t length=strlen(cursor);if(length&&cursor[length-1]=='\r')cursor[--length]=0;
        if(length>1024U)goto invalid;
        for(size_t i=0;i<length;i++)if((unsigned char)cursor[i]<32U&&cursor[i]!='\t')goto invalid;
        char *s=trim(cursor);cursor=next;
        if(!*s||*s=='#')continue;
        if(*s=='['){
            size_t n=strlen(s);
            if(!version||n<3U||s[n-1]!=']'||c->count==GKD_MENU_PROFILE_MAX)goto invalid;
            if(c->count&&complete(c,seen))goto invalid;
            s[n-1]=0;if(!id_valid(s+1))goto invalid;
            struct gkd_menu_profile *p=&c->profiles[c->count++];
            strcpy(p->id,s+1);p->hold_ms=100U;seen=0;continue;
        }
        char *equals=strchr(s,'=');if(!equals)goto invalid;
        *equals=0;char *key=trim(s),*value=trim(equals+1);
        if(!c->count){
            if(version||strcmp(key,"version")||(strcmp(value,"1")&&strcmp(value,"2")))goto invalid;
            version=(unsigned)(value[0]-'0');continue;
        }
        struct gkd_menu_profile *p=&c->profiles[c->count-1U];unsigned bit;
        if(version==2U&&!strncmp(key,"map.",4)){if(mapping(key+4,value,p))goto invalid;continue;}
        if(!strcmp(key,"opk_sha256"))bit=1U;
        else if(!strcmp(key,"desktop"))bit=2U;
        else if(!strcmp(key,"exec"))bit=4U;
        else if(!strcmp(key,"action"))bit=8U;
        else if(!strcmp(key,"keys"))bit=16U;
        else if(!strcmp(key,"hold_ms"))bit=32U;
        else if(version==2U&&!strcmp(key,"rom"))bit=64U;
        else goto invalid;
        if(seen&bit)goto invalid;
        seen|=bit;
        switch(bit){
        case 1U:if(!hash_valid(value))goto invalid;strcpy(p->opk_sha256,value);break;
        case 2U:if(!entry_valid(value))goto invalid;strcpy(p->desktop,value);break;
        case 4U:if(!entry_valid(value))goto invalid;strcpy(p->executable,value);break;
        case 8U:
            if(!strcmp(value,"native"))p->action=GKD_MENU_NATIVE;
            else if(!strcmp(value,"chord"))p->action=GKD_MENU_CHORD;
            else if(!strcmp(value,"none"))p->action=GKD_MENU_NONE;
            else if(!strcmp(value,"disabled"))p->action=GKD_MENU_DISABLED;
            else goto invalid;
            break;
        case 16U:if(keys(value,p))goto invalid;break;
        case 32U:if(number(value,500U,&p->hold_ms)||p->hold_ms<20U)goto invalid;break;
        case 64U:if(!rom_valid(value))goto invalid;strcpy(p->rom,value);break;
        }
    }
    if(!version||(c->count&&complete(c,seen)))goto invalid;
    c->version=version;*out=*c;result=0;goto done;
invalid:errno=EPROTO;if(bad_line)*bad_line=line;
done:{int saved=errno;free(copy);free(c);errno=saved;return result;}
}
int gkd_menu_config_load_fd(int fd,struct gkd_menu_config *out,unsigned *bad_line)
{
    if(bad_line)*bad_line=0;
    if(fd<0||!out){errno=EINVAL;return -1;}
    struct stat st;int result=-1;char *data=NULL;size_t used=0;
    if(fstat(fd,&st))goto done;
    if(!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&0022)){errno=EPERM;goto done;}
    if(st.st_size<1||st.st_size>(off_t)GKD_MENU_CONFIG_BYTES){errno=EFBIG;goto done;}
    data=malloc(GKD_MENU_CONFIG_BYTES+1U);if(!data){errno=ENOMEM;goto done;}
    for(;;){
        ssize_t n=pread(fd,data+used,GKD_MENU_CONFIG_BYTES+1U-used,(off_t)used);
        if(n<0&&errno==EINTR)continue;
        if(n<0)goto done;
        if(!n)break;
        used+=(size_t)n;if(used>GKD_MENU_CONFIG_BYTES){errno=EFBIG;goto done;}
    }
    struct stat after;
    if(fstat(fd,&after))goto done;
    if(st.st_size!=(off_t)used||st.st_size!=after.st_size||st.st_mtim.tv_sec!=after.st_mtim.tv_sec||
       st.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||st.st_ctim.tv_sec!=after.st_ctim.tv_sec||
       st.st_ctim.tv_nsec!=after.st_ctim.tv_nsec){errno=ESTALE;goto done;}
    result=gkd_menu_config_parse(data,used,out,bad_line);
done:{int saved=errno;free(data);errno=saved;return result;}
}
int gkd_menu_config_load(const char *path,struct gkd_menu_config *out,unsigned *bad_line)
{
 if(!path){errno=EINVAL;return -1;}
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
 int rc=gkd_menu_config_load_fd(fd,out,bad_line),saved=errno;
 close(fd);errno=saved;return rc;
}
int gkd_menu_config_select(const struct gkd_menu_config *c,const char *hash,const char *desktop,
                          const char *executable,struct gkd_menu_profile *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!c||!out||c->count>GKD_MENU_PROFILE_MAX||!hash_valid(hash)||
       !entry_valid(desktop)||!entry_valid(executable)){errno=EINVAL;return -1;}
    for(unsigned i=0;i<c->count;i++){
        const struct gkd_menu_profile *p=&c->profiles[i];
        if(!p->rom[0]&&!strcmp(p->opk_sha256,hash)&&!strcmp(p->desktop,desktop)&&!strcmp(p->executable,executable)){
            struct gkd_menu_profile merged=*p;
   /* Inherit emulator mappings, then replace only explicitly configured game
    * sources. The complete game profile retains its exact MENU contract. */
   merged.map_count=out->map_count;memcpy(merged.maps,out->maps,sizeof(merged.maps));
   for(unsigned k=0;k<p->map_count;k++) {
    unsigned at;
    for(at=0;at<merged.map_count;at++)if(merged.maps[at].source==p->maps[k].source)break;
    if(at==GKD_INPUT_ROUTE_MAPS){memset(out,0,sizeof(*out));errno=E2BIG;return -1;}
    merged.maps[at]=p->maps[k];if(at==merged.map_count)merged.map_count++;
   }
   *out=merged;return 1;
        }
    }
    return 0;
}
const char *gkd_menu_action_name(enum gkd_menu_action action)
{
    switch(action){
    case GKD_MENU_DISABLED:return "disabled";
    case GKD_MENU_NATIVE:return "native";
    case GKD_MENU_CHORD:return "chord";
    case GKD_MENU_NONE:return "none";
    }
    return "invalid";
}

int gkd_menu_config_select_game(const struct gkd_menu_config *c,const char *hash,const char *desktop,
 const char *executable,int argc,char *const argv[],struct gkd_menu_profile *out)
{
 int base=gkd_menu_config_select(c,hash,desktop,executable,out),matched=0;
 if(base<0)return -1;
 if(argc<0||argc>4096||(argc&&!argv)){memset(out,0,sizeof(*out));errno=EINVAL;return -1;}
 for(unsigned i=0;i<c->count;i++) {
  const struct gkd_menu_profile *p=&c->profiles[i];
  if(!p->rom[0]||strcmp(p->opk_sha256,hash)||strcmp(p->desktop,desktop)||strcmp(p->executable,executable))continue;
  int found=0;
  for(int j=0;j<argc;j++)if(argv[j]&&!strcmp(p->rom,argv[j]))found=1;
  if(found) {
   if(matched++){memset(out,0,sizeof(*out));errno=EINVAL;return -1;}
   struct gkd_menu_profile merged=*p;
   /* Inherit emulator mappings, then replace only explicitly configured game
    * sources. The complete game profile retains its exact MENU contract. */
   merged.map_count=out->map_count;memcpy(merged.maps,out->maps,sizeof(merged.maps));
   for(unsigned k=0;k<p->map_count;k++) {
    unsigned at;
    for(at=0;at<merged.map_count;at++)if(merged.maps[at].source==p->maps[k].source)break;
    if(at==GKD_INPUT_ROUTE_MAPS){memset(out,0,sizeof(*out));errno=E2BIG;return -1;}
    merged.maps[at]=p->maps[k];if(at==merged.map_count)merged.map_count++;
   }
   *out=merged;
  }
 }
 return matched?1:base;
}
static int add_map(struct gkd_menu_vt_config *c,unsigned short source,unsigned short target)
{
 if(source==c->trigger){errno=EINVAL;return -1;}
 for(unsigned i=0;i<c->map_count;i++)if(c->maps[i].source==source){c->maps[i].target=target;return 0;}
 if(c->map_count==GKD_INPUT_ROUTE_MAPS){errno=E2BIG;return -1;}
 c->maps[c->map_count++]=(struct gkd_input_route_map){source,target};return 0;
}
int gkd_input_route_compile(unsigned style,unsigned short trigger,const struct gkd_menu_profile *p,
 struct gkd_menu_vt_config *out)
{
 if(!out||!p||style>=GKD_INPUT_STYLE_COUNT||!trigger||trigger>KEY_MAX||
    p->map_count>GKD_INPUT_ROUTE_MAPS||p->key_count>GKD_MENU_CHORD_MAX){errno=EINVAL;return -1;}
 memset(out,0,sizeof(*out));
 if(style==GKD_INPUT_RAW)return 0;
 out->version=GKD_MENU_VT_VERSION;out->hold_ms=100;out->trigger=trigger;
 /* Original A/B and X/Y output roles are exchanged for Xbox/PS physical
  * conventions. The PS style shares the functional layout, with PS prompts. */
 if(add_map(out,KEY_LEFTCTRL,KEY_LEFTALT)||add_map(out,KEY_LEFTALT,KEY_LEFTCTRL)||
    add_map(out,KEY_LEFTSHIFT,KEY_SPACE)||add_map(out,KEY_SPACE,KEY_LEFTSHIFT))return -1;
 for(unsigned i=0;i<p->map_count;i++)
  if(add_map(out,p->maps[i].source,p->maps[i].target))return -1;
 if(p->action==GKD_MENU_NATIVE){out->count=1;out->keys[0]=KEY_HOME;}
 else if(p->action==GKD_MENU_CHORD){
  out->count=p->key_count;out->hold_ms=p->hold_ms;memcpy(out->keys,p->keys,sizeof(out->keys));
 }
 return 1;
}
