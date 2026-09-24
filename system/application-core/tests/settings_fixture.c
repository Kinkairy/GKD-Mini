/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-settings.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void write_text(const char *path,const char *text)
{FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));}
int main(int argc,char **argv)
{
 struct gkd_app_settings s,old;char original[65537],changed[65537];size_t bytes;
 assert(argc==2);
 FILE *f=fopen(argv[1],"r");assert(f);bytes=fread(original,1,sizeof(original)-1,f);assert(!ferror(f)&&feof(f));assert(!fclose(f));original[bytes]=0;
 assert(!gkd_app_settings_load(argv[1],&s));
 assert(s.effects==1&&s.frontend_timeout==60000);
 assert(s.battery_low==5&&s.battery_critical==5&&s.battery_suspend_delay_ms==15000);
 assert(s.screenshot_pair[0]==8U&&s.screenshot_pair[1]==4U);
 assert(s.usb_notify_ms==1600);
 assert(s.usb_default==0&&s.power_default==0&&s.auto_suspend_seconds==600);
 assert(s.battery_curve[4]==4200&&s.battery_leds[2]==75&&!strcmp(s.keys[2],"KEY_LEFTCTRL"));
 assert(!strcmp(s.brightness_key,"KEY_END"));
 /* Existing explicit zero remains valid after changing fresh defaults. */
 strcpy(changed,original);char *sleep=strstr(changed,"auto_suspend_timeout_seconds=600\n");assert(sleep);
 memmove(sleep+strlen("auto_suspend_timeout_seconds="),sleep+strlen("auto_suspend_timeout_seconds=")+2,
         strlen(sleep+strlen("auto_suspend_timeout_seconds=")+2)+1);
 write_text(argv[1],changed);assert(!gkd_app_settings_load(argv[1],&s)&&s.auto_suspend_seconds==0);
 write_text(argv[1],original);assert(!gkd_app_settings_load(argv[1],&s)&&s.auto_suspend_seconds==600);
 old=s;
 strcpy(changed,original);char *shot=strstr(changed,"screenshot_hotkey=MENU+L1\n");assert(shot);
 char *value=shot+strlen("screenshot_hotkey=");
 memmove(value+5,value+7,strlen(value+7)+1);memcpy(value,"L1+L2",5);
 write_text(argv[1],changed);assert(gkd_app_settings_load(argv[1],&s)<0&&!memcmp(&s,&old,sizeof(s)));
 write_text(argv[1],original);
 strcpy(changed,original);char *usb=strstr(changed,"usb_feedback_timeout_ms=1600\n");assert(usb);
 memcpy(usb+strlen("usb_feedback_timeout_ms="),"9999",4);write_text(argv[1],changed);
 assert(gkd_app_settings_load(argv[1],&s)<0&&!memcmp(&s,&old,sizeof(s)));
 write_text(argv[1],original);

 assert(bytes+64<sizeof(changed));
 strcpy(changed,original);strcat(changed,"ui_dynamic_effects=disabled\n");write_text(argv[1],changed);
 assert(gkd_app_settings_load(argv[1],&s)<0&&!memcmp(&s,&old,sizeof(s)));
 strcpy(changed,original);
 char *start=strstr(changed,"battery_voltage_curve_mv="),*end;
 assert(start&&(end=strchr(start,'\n')));memmove(start,end+1,strlen(end+1)+1);
 write_text(argv[1],changed);assert(gkd_app_settings_load(argv[1],&s)<0&&!memcmp(&s,&old,sizeof(s)));
 write_text(argv[1],original);assert(!chmod(argv[1],0666));
 assert(gkd_app_settings_load(argv[1],&s)<0&&errno==EPERM);assert(!chmod(argv[1],0600));
 const char *link="/tmp/gkd-settings-link";assert(!symlink(argv[1],link));
 assert(gkd_app_settings_load(link,&s)<0);assert(!unlink(link));
 assert(!gkd_app_settings_load(argv[1],&s));
 puts("GKD_APP_SETTINGS_FIXTURE=PASS schema-defaults/required/duplicate/atomic/permissions/symlink");
 return 0;
}
