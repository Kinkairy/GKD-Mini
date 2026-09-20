/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-ui-language.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *const texts[GKD_UI_TEXT_COUNT][2]={
#define GKD_UI_TEXT_ROW(id,key,en,cn) {en,cn},
#include "gkd-ui-language.def"
#undef GKD_UI_TEXT_ROW
};
static const char *const names[GKD_UI_TEXT_COUNT]={
#define GKD_UI_TEXT_ROW(id,key,en,cn) #key,
#include "gkd-ui-language.def"
#undef GKD_UI_TEXT_ROW
};
const char *gkd_ui_text(enum gkd_ui_language language,enum gkd_ui_text key)
{
 if((unsigned)language>GKD_UI_CN||(unsigned)key>=GKD_UI_TEXT_COUNT)return NULL;
 return texts[(unsigned)key][(unsigned)language];
}
const char *gkd_ui_catalog_text(const struct gkd_ui_catalog *catalog,enum gkd_ui_language language,enum gkd_ui_text key)
{
 const char *fallback=gkd_ui_text(language,key);
 if(!fallback)return NULL;
 return catalog&&catalog->values[key][language][0]?catalog->values[key][language]:fallback;
}
/* Bounded valid UTF-8, no control characters or overlong/surrogate encodings. */
static int text_valid(const char *text,unsigned limit,unsigned language)
{
 const unsigned char *p=(const unsigned char *)text;unsigned count=0;
 if(!*p||strlen(text)>=64U)return 0;
 while(*p){
  const unsigned char *start=p;uint32_t cp;unsigned n;
  if(*p<0x80U){cp=*p++;n=0;}
  else if(*p>=0xc2U&&*p<=0xdfU){cp=*p++&31U;n=1;}
  else if(*p>=0xe0U&&*p<=0xefU){cp=*p++&15U;n=2;}
  else if(*p>=0xf0U&&*p<=0xf4U){cp=*p++&7U;n=3;}
  else return 0;
  if(n&&!language)return 0;
  unsigned remaining=n;
  while(remaining--){if((*p&0xc0U)!=0x80U)return 0;cp=(cp<<6)|(*p++&63U);}
  if((n==1&&cp<0x80U)||(n==2&&cp<0x800U)||(n==3&&cp<0x10000U)||cp>0x10ffffU||
     (cp>=0xd800U&&cp<=0xdfffU)||cp<32U||(cp>=127U&&cp<=159U)||++count>limit)return 0;
  if(n){char glyph[5]={0};int found=0;memcpy(glyph,start,n+1U);
   for(unsigned key=0;key<GKD_UI_TEXT_COUNT;key++)if(strstr(texts[key][1],glyph)){found=1;break;}
   if(!found)return 0;
  }
 }
 return 1;
}
int gkd_ui_catalog_load(struct gkd_ui_catalog *out,const char *path)
{
 struct gkd_ui_catalog candidate={0};struct stat st;char *line=NULL;size_t capacity=0,total=0;ssize_t bytes;
 unsigned char seen[GKD_UI_TEXT_COUNT][2]={{0}};int saved=0,fd;
 if(!out||!path){errno=EINVAL;return -1;}
 fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0){if(errno==ENOENT){*out=candidate;return 0;}return -1;}
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||(st.st_mode&0022)||st.st_size>65536){close(fd);errno=EPERM;return -1;}
 FILE *file=fdopen(fd,"r");if(!file){saved=errno;close(fd);errno=saved;return -1;}
 while((bytes=getline(&line,&capacity,file))>=0){
  total+=(size_t)bytes;
  if(total>65536U||!bytes||line[bytes-1]!='\n'||memchr(line,0,(size_t)bytes)){saved=EPROTO;break;}
  line[--bytes]=0;
  if(strncmp(line,"app_text_",9U))continue;
  char *value=strchr(line,'=');if(!value){saved=EINVAL;break;}*value++=0;
  int found=0;
  for(unsigned key=0;key<GKD_UI_TEXT_COUNT;key++)for(unsigned lang=0;lang<2U;lang++){
   char name[80];snprintf(name,sizeof(name),"app_text_%s_%s",names[key],lang?"zh":"en");
   if(strcmp(line,name))continue;
   int body=key==GKD_UI_TEXT_INSERT_CARD||key==GKD_UI_TEXT_LOADING_CARD;
   unsigned limit=(key==GKD_UI_TEXT_YES||key==GKD_UI_TEXT_NO)?(lang?3U:7U):((lang&&!body)?9U:20U);
   if(seen[key][lang]++||!text_valid(value,limit,body?0U:lang)){saved=EINVAL;break;}
   strcpy(candidate.values[key][lang],value);found=1;
  }
  if(saved||!found){saved=EINVAL;break;}
 }
 if(ferror(file)&&!saved)saved=EIO;
 free(line);if(fclose(file)&&!saved)saved=errno;
 if(saved){errno=saved;return -1;}
 *out=candidate;return 0;
}
