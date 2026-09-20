/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_INPUT_STYLE_H
#define GKD_INPUT_STYLE_H
#include <string.h>
enum gkd_input_style { GKD_INPUT_RAW, GKD_INPUT_XBOX, GKD_INPUT_PS, GKD_INPUT_STYLE_COUNT };
static inline const char *gkd_input_style_name(unsigned style)
{
 static const char *const names[]={"raw","xbox","ps"};
 return style<GKD_INPUT_STYLE_COUNT?names[style]:NULL;
}
static inline int gkd_input_style_parse(const char *text,unsigned *style)
{
 if(!text||!style)return -1;
 for(unsigned i=0;i<GKD_INPUT_STYLE_COUNT;i++)
  if(!strcmp(text,gkd_input_style_name(i))){*style=i;return 0;}
 return -1;
}
#endif
