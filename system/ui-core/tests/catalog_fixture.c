/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-ui-language.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void content(const char *s)
{
 FILE *f=fopen("/tmp/gkd-catalog.conf","w");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f));
 assert(!chmod("/tmp/gkd-catalog.conf",0600));
}
int main(void)
{
 struct gkd_ui_catalog c={0},before;
 assert(!gkd_ui_catalog_load(&c,"/tmp/nonexistent-catalog"));
 assert(!strcmp(gkd_ui_catalog_text(&c,GKD_UI_EN,GKD_UI_TEXT_YES),"SURE"));
 content("ui_language=en\napp_text_bat_low_en=LOW POWER\napp_text_bat_low_zh=电量过低\n");
 assert(!gkd_ui_catalog_load(&c,"/tmp/gkd-catalog.conf"));before=c;
 assert(!strcmp(gkd_ui_catalog_text(&c,GKD_UI_EN,GKD_UI_TEXT_BAT_LOW),"LOW POWER"));
 assert(!strcmp(gkd_ui_catalog_text(&c,GKD_UI_CN,GKD_UI_TEXT_BAT_LOW),"电量过低"));
 content("app_text_insert_card_zh=INSERT GAME CARD\napp_text_loading_card_zh=LOADING GAME CARD\n");
 assert(!gkd_ui_catalog_load(&c,"/tmp/gkd-catalog.conf"));before=c;
 assert(!strcmp(gkd_ui_catalog_text(&c,GKD_UI_CN,GKD_UI_TEXT_INSERT_CARD),"INSERT GAME CARD"));
 assert(!strcmp(gkd_ui_catalog_text(&c,GKD_UI_CN,GKD_UI_TEXT_LOADING_CARD),"LOADING GAME CARD"));
 const char *bad[]={"app_text_bat_low_en=XXXXXXXXXXXXXXXXXXXXX\n",
  "app_text_yes_en=TOO LONG\n","app_text_yes_zh=电量过低\n","app_text_bat_low_zh=测试\n",
  "app_text_bat_low_en=电量低\n","app_text_bat_low_en=\xc0\xaf\n","app_text_bat_low_en=BAD\1TEXT\n",
  "app_text_bat_low_en=POWER\napp_text_bat_low_en=OTHER\n","app_text_unknown_en=WHAT\n",
  "app_text_insert_card_zh=游戏卡\n","app_text_loading_card_zh=XXXXXXXXXXXXXXXXXXXXX\n",
  "app_text_bat_low_en=MISSING NEWLINE"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
  content(bad[i]);assert(gkd_ui_catalog_load(&c,"/tmp/gkd-catalog.conf")<0);
  assert(!memcmp(&c,&before,sizeof(c)));
 }
 content("app_text_bat_low_en=POWER\n");assert(!chmod("/tmp/gkd-catalog.conf",0666));
 assert(gkd_ui_catalog_load(&c,"/tmp/gkd-catalog.conf")<0&&errno==EPERM);
 assert(!symlink("/tmp/gkd-catalog.conf","/tmp/gkd-catalog-link"));
 assert(gkd_ui_catalog_load(&c,"/tmp/gkd-catalog-link")<0);
 assert(!unlink("/tmp/gkd-catalog-link"));assert(!unlink("/tmp/gkd-catalog.conf"));
 puts("GKD_UI_CATALOG=PASS valid/default/length/glyph/UTF8/atomic/security");
 return 0;
}
