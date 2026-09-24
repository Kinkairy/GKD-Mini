/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-settings.h"
#include "gkd-input-style.h"
#include "../../ui-core/include/gkd-input-keys.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define GKD_SCREENSHOT_PATH(root,bytes) static const char screenshot_root[]=root; enum { SCREENSHOT_BYTES=bytes };
#define GKD_SCREENSHOT_HOTKEY(name,first,second)
#include "gkd-app-settings-contract.def"
#undef GKD_SCREENSHOT_PATH
#undef GKD_SCREENSHOT_HOTKEY
typedef char screenshot_contract_size[(sizeof(((struct gkd_app_settings *)0)->screenshot_directory)==SCREENSHOT_BYTES)?1:-1];
struct screenshot_hotkey { const char *name; unsigned first,second; };
static const struct screenshot_hotkey screenshot_hotkeys[]={
#define GKD_SCREENSHOT_PATH(root,bytes)
#define GKD_SCREENSHOT_HOTKEY(name,first,second) {name,first,second},
#include "gkd-app-settings-contract.def"
#undef GKD_SCREENSHOT_PATH
#undef GKD_SCREENSHOT_HOTKEY
};
enum field_type {UINT_FIELD,KEY_FIELD,EFFECT_FIELD,USB_FIELD,POWER_FIELD,CURVE_FIELD,LED_FIELD,HOTKEY_FIELD,PATH_FIELD,LANGUAGE_FIELD,STYLE_FIELD};
struct field {const char *name;enum field_type type;size_t offset;unsigned low,high;};
#define U(k,m,l,h) {k,UINT_FIELD,offsetof(struct gkd_app_settings,m),l,h}
#define T(k,t,m) {k,t,offsetof(struct gkd_app_settings,m),0,0}
static const struct field fields[]={
 T("ui_dynamic_effects",EFFECT_FIELD,effects),
 T("ui_show_fps",EFFECT_FIELD,show_fps),
 T("ui_language",LANGUAGE_FIELD,chinese),
 T("input_style",STYLE_FIELD,input_style),
 T("input_map_menu",KEY_FIELD,menu_key),
 T("input_map_brightness",KEY_FIELD,brightness_key),
 T("input_map_dpad_left",KEY_FIELD,left_key),
 T("input_map_dpad_right",KEY_FIELD,right_key),
 U("usb_frontend_ready_timeout_ms",frontend_timeout,5000,120000),
 U("usb_feedback_timeout_ms",usb_notify_ms,200,5000),
 T("usb_chooser_default",USB_FIELD,usb_default),
 T("power_menu_default",POWER_FIELD,power_default),
 U("auto_suspend_timeout_seconds",auto_suspend_seconds,0,86400),
 U("battery_poll_ms",battery_poll_ms,1000,60000),
 U("battery_suspend_enabled",battery_suspend_enabled,0,1),
 U("battery_suspend_delay_ms",battery_suspend_delay_ms,1000,600000),
 U("battery_low_percent",battery_low,2,50),
 U("battery_critical_percent",battery_critical,1,25),
 U("battery_warning_hysteresis_percent",battery_hysteresis,1,10),
 T("battery_voltage_curve_mv",CURVE_FIELD,battery_curve),
 T("battery_led_thresholds",LED_FIELD,battery_leds),
 T("input_map_dpad_up",KEY_FIELD,keys[0]),
 T("input_map_dpad_down",KEY_FIELD,keys[1]),
 T("input_map_a",KEY_FIELD,keys[2]),
 T("input_map_b",KEY_FIELD,keys[3]),
 T("input_map_l1",KEY_FIELD,keys[4]),
 T("input_map_r1",KEY_FIELD,keys[5]),
 T("input_map_l2",KEY_FIELD,keys[6]),
 T("input_map_r2",KEY_FIELD,keys[7]),
 T("screenshot_hotkey",HOTKEY_FIELD,screenshot_pair),
 T("screenshot_output_dir",PATH_FIELD,screenshot_directory),
 U("screenshot_osd_timeout_ms",screenshot_notify_ms,0,5000),
 U("battery_notify_timeout_ms",battery_notify_ms,200,5000)
};
typedef char fields_fit_mask[(sizeof(fields)/sizeof(fields[0]) <= 64U) ? 1 : -1];
#undef U
#undef T
static int uint_value(const char *text,unsigned low,unsigned high,unsigned *value)
{
 unsigned n=0;
 if(!*text)return -1;
 for(;*text;text++){
  if(*text<'0'||*text>'9'||n>high/10U)return -1;
  n=n*10U+(unsigned)(*text-'0');if(n>high)return -1;
 }
 if(n<low)return -1;
 *value=n;return 0;
}
static int list_value(char *text,unsigned count,unsigned low,unsigned high,unsigned *values)
{
 unsigned i;
 for(i=0;i<count;i++){
  char *next=strchr(text,',');
  if((i+1U<count)!=!!next)return -1;
  if(next)*next=0;
  if(uint_value(text,low,high,&values[i])||(i&&values[i]<=values[i-1U]))return -1;
  if(next)text=next+1;
 }
 return 0;
}
static int field_value(const struct field *field,char *text,struct gkd_app_settings *s)
{
 void *dest=(unsigned char *)s+field->offset;unsigned *number=dest;
 switch(field->type){
 case PATH_FIELD:
  if(strlen(text)>=SCREENSHOT_BYTES||strncmp(text,screenshot_root,sizeof(screenshot_root)-1U)||!text[sizeof(screenshot_root)-1U]||
     strstr(text,"//")||strstr(text,"/../")||strstr(text,"/./")||
     !strcmp(text+strlen(text)-3U,"/..")||!strcmp(text+strlen(text)-2U,"/."))
   return -1;
  for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32U||*p==127U)return -1;
  strcpy(dest,text);return 0;
 case HOTKEY_FIELD:{
  for(unsigned i=0;i<sizeof(screenshot_hotkeys)/sizeof(screenshot_hotkeys[0]);i++){
   if(!strcmp(text,screenshot_hotkeys[i].name)){
    number[0]=screenshot_hotkeys[i].first;number[1]=screenshot_hotkeys[i].second;return 0;
   }
  }
  return -1;
 }
 case UINT_FIELD:return uint_value(text,field->low,field->high,number);
 case KEY_FIELD:{
  unsigned short code;
  if(strlen(text)>=24U||gkd_input_key_code(text,&code))return -1;
  strcpy(dest,text);return 0;
 }
 case CURVE_FIELD:return list_value(text,5U,2800U,4500U,number);
 case LED_FIELD:return list_value(text,3U,1U,99U,number);
 case STYLE_FIELD:return gkd_input_style_parse(text,number);
 case LANGUAGE_FIELD:
  if(!strcmp(text,"en")){*number=0;return 0;}
  if(!strcmp(text,"zh")){*number=1;return 0;}return -1;
 case EFFECT_FIELD:
  if(!strcmp(text,"enabled")){*number=1;return 0;}
  if(!strcmp(text,"disabled")){*number=0;return 0;}return -1;
 case USB_FIELD:
  if(!strcmp(text,"charge")){*number=0;return 0;}
  if(!strcmp(text,"storage")){*number=1;return 0;}
  if(!strcmp(text,"debug")){*number=2;return 0;}return -1;
 case POWER_FIELD:
  if(!strcmp(text,"suspend")){*number=0;return 0;}
  if(!strcmp(text,"reboot")){*number=1;return 0;}
  if(!strcmp(text,"shutdown")){*number=2;return 0;}return -1;
 }
 return -1;
}
int gkd_app_settings_load(const char *path,struct gkd_app_settings *out)
{
 struct gkd_app_settings s={0};struct stat st;FILE *f;int fd,rc=-1,saved;
 char *line=NULL;size_t capacity=0,total=0;ssize_t bytes;uint64_t seen=0;
 if(!path||!out){errno=EINVAL;return -1;}
 fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022)||st.st_size>65536){
  close(fd);errno=EPERM;return -1;
 }
 f=fdopen(fd,"r");if(!f){saved=errno;close(fd);errno=saved;return -1;}
 while((bytes=getline(&line,&capacity,f))>=0){
  char *value;
  total+=(size_t)bytes;
  if(total>65536U||!bytes||line[bytes-1]!='\n'||memchr(line,0,(size_t)bytes)){errno=EPROTO;goto end;}
  line[--bytes]=0;
  if(!bytes||line[0]=='#')continue;
  value=strchr(line,'=');if(!value){errno=EPROTO;goto end;}*value++=0;
  for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
   if(strcmp(line,fields[i].name))continue;
   if((seen&(UINT64_C(1)<<i))||field_value(&fields[i],value,&s)){errno=EINVAL;goto end;}
   seen|=UINT64_C(1)<<i;break;
  }
 }
 if(ferror(f))goto end;
 if(seen!=(UINT64_MAX>>(64U-sizeof(fields)/sizeof(fields[0])))||s.battery_critical>s.battery_low||
    s.battery_low+s.battery_hysteresis>100U){errno=EINVAL;goto end;}
 for(unsigned i=0;i<8U;i++)for(unsigned j=0;j<i;j++)if(!strcmp(s.keys[i],s.keys[j])){errno=EINVAL;goto end;}
 {
  const char *extra[]={s.menu_key,s.brightness_key,s.left_key,s.right_key};
  for(unsigned i=0;i<4U;i++){
   for(unsigned j=0;j<8U;j++)if(!strcmp(extra[i],s.keys[j])){errno=EINVAL;goto end;}
   for(unsigned j=0;j<i;j++)if(!strcmp(extra[i],extra[j])){errno=EINVAL;goto end;}
  }
 }
 rc=0;
end:
 saved=errno;free(line);if(fclose(f)&&!rc){rc=-1;saved=errno;}
 if(!rc)*out=s;
 errno=saved;return rc;
}
