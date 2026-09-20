/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-ui.h"
#include "gkd-ui-language.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
 assert(argc==5);
 struct gkd_ui_config config;struct gkd_ui_font font={0},cn={0},small={0};
 uint16_t pixels[320U*240U],reference[320U*240U],before[320U*240U];
 struct gkd_ui_surface surface={pixels,320,240,320},common={reference,320,240,320};
 gkd_ui_config_defaults(&config);
 assert(!gkd_ui_font_load(&font,argv[1])&&!gkd_ui_font_load(&cn,argv[3])&&!gkd_ui_font_load(&small,argv[4]));
 cn.latin=small.latin=&font;cn.compact=&small;
 char current[64],target[64];
 const char *lines[]={current,target,"",NULL,"MORE INFORMATION","LINE 6","LINE 7","END"};
 struct gkd_ui_confirmation info={"SYSTEM UPDATE",lines,4,0,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED};
 struct gkd_ui_menu_item rows[]={{"ONE",0},{"TWO",1},{"THREE",2},{"FOUR",3},{"FIVE",4}};
 struct gkd_ui_menu menu={"SYSTEM UPDATE",rows,5,0,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED,NULL,NULL};
 for(unsigned lang=0;lang<2U;lang++){
  snprintf(current,sizeof(current),"%s  3.5",gkd_ui_text(GKD_UI_EN,GKD_UI_TEXT_CURRENT_VERSION));
  snprintf(target,sizeof(target),"%s   3.6",gkd_ui_text(GKD_UI_EN,GKD_UI_TEXT_TARGET_VERSION));
  info.title=gkd_ui_text((enum gkd_ui_language)lang,GKD_UI_TEXT_SYSTEM_UPDATE);
  lines[3]=gkd_ui_text(GKD_UI_EN,GKD_UI_TEXT_CONFIRM_UPDATE);
  const struct gkd_ui_font *face=lang?&cn:&font;
  assert(!gkd_ui_render_confirmation_info(&surface,&config,face,&info));
  assert(!gkd_ui_render_menu(&common,&config,face,&menu));
  assert(!memcmp(pixels+206U*320U,reference+206U*320U,34U*320U*sizeof(*pixels)));
  /* Outer frame matches the union of five menu rows, with no inner dividers. */
  for(unsigned x=40;x<280;x++) {
   assert(pixels[48U*320U+x]==reference[48U*320U+x]);
   assert(pixels[198U*320U+x]==reference[198U*320U+x]);
  }
  for(unsigned y=49;y<198;y++) assert(pixels[y*320U+40U]==config.highlight);
  assert(pixels[75U*320U+44U]!=config.highlight);
  char output[512];snprintf(output,sizeof(output),"%s/confirmation-%s.rgb565",argv[2],lang?"cn":"en");
  assert(!gkd_ui_write_raw(&surface,output));
  info.action_a=info.action_b=menu.action_a=menu.action_b=GKD_UI_ACTION_DISABLED;
  assert(!gkd_ui_render_confirmation_info(&surface,&config,face,&info));
  assert(!gkd_ui_render_menu(&common,&config,face,&menu));
  assert(!memcmp(pixels+206U*320U,reference+206U*320U,34U*320U*sizeof(*pixels)));
  info.action_a=info.action_b=menu.action_a=menu.action_b=GKD_UI_ACTION_ENABLED;
 }
 info.title="SYSTEM UPDATE";lines[3]="INSTALL THIS UPDATE?";
 info.count=8;
 assert(!gkd_ui_render_confirmation_info(&surface,&config,&font,&info));
 assert(pixels[48U*320U+286U]==config.normal);
 memcpy(before,pixels,sizeof(pixels));info.first=1;
 assert(!gkd_ui_render_confirmation_info(&surface,&config,&font,&info));
 assert(pixels[198U*320U+286U]==config.normal);
 assert(memcmp(before,pixels,sizeof(pixels)));
 memcpy(before,pixels,sizeof(pixels));info.first=2;
 assert(gkd_ui_render_confirmation_info(&surface,&config,&font,&info)<0);
 assert(!memcmp(before,pixels,sizeof(pixels)));
 info.first=0;info.count=33;
 assert(gkd_ui_render_confirmation_info(&surface,&config,&font,&info)<0);
 assert(!memcmp(before,pixels,sizeof(pixels)));
 info.count=4;lines[3]="THIS TEXT IS TOO LONG FOR THE INFORMATION PANEL";
 assert(gkd_ui_render_confirmation_info(&surface,&config,&font,&info)<0);
 assert(!memcmp(before,pixels,sizeof(pixels)));
 assert(gkd_ui_render_confirmation_info(&surface,&config,&font,NULL)<0);
 assert(!memcmp(before,pixels,sizeof(pixels)));
 struct gkd_ui_text_layout layout,saved_layout;
 assert(!gkd_ui_layout_text(&config,&cn,"系统更新\n\nCURRENT  18.20.30\r\nTARGET  18.20.31",&layout));
 assert(layout.count==4U&&!layout.lines[0][0]&&!layout.lines[1][0]);
 assert(!strcmp(layout.lines[2],"CURRENT  18.20.30")&&!strcmp(layout.lines[3],"TARGET  18.20.31"));
 assert(!gkd_ui_layout_text(&config,&cn,"中文 INFO 更新 18.20.31",&layout));
 assert(layout.count==1U&&!strcmp(layout.lines[0]," INFO  18.20.31"));
 for(const unsigned char *v=(const unsigned char *)layout.lines[0];*v;v++)assert(*v<127U);
 char body[512];memset(body,'W',sizeof(body)-1U);body[sizeof(body)-1U]=0;
 assert(!gkd_ui_layout_text(&config,&font,body,&layout)&&layout.count>7U);
 info=(struct gkd_ui_confirmation){"DETAILS",layout.lines,layout.count,0U,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED};
 assert(!gkd_ui_render_confirmation_info(&surface,&config,&font,&info));
 info.first=layout.count-gkd_ui_confirmation_visible(&config);
 assert(!gkd_ui_render_confirmation_info(&surface,&config,&font,&info));
 saved_layout=layout;
 const char *bad[]={"","\033bad","\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe7", "龍"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
  assert(gkd_ui_layout_text(&config,&cn,bad[i],&layout)<0);
  assert(!memcmp(&layout,&saved_layout,sizeof(layout)));
 }
 char excessive[2049];memset(excessive,'A',sizeof(excessive)-1U);excessive[sizeof(excessive)-1U]=0;
 assert(gkd_ui_layout_text(&config,&font,excessive,&layout)<0&&!memcmp(&layout,&saved_layout,sizeof(layout)));
 puts("GENERIC_TEXT_LAYOUT_PASS english-body/chinese-filter/native-font-wrap/paragraphs/CRLF/scroll/reject-invalid-or-overflow-without-truncation");
 gkd_ui_font_release(&small);gkd_ui_font_release(&cn);gkd_ui_font_release(&font);
 puts("CONFIRMATION_INFO_PASS five-row-outer-frame/plain-text/shared-footer/overflow/two-languages/atomic-rejection");
 return 0;
}
