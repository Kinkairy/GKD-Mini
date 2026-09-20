/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-app-menu-config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    int match=(argc==6||argc==7)&&!strcmp(argv[1],"match");
    if(!match&&(argc!=3||strcmp(argv[1],"check"))){
        fprintf(stderr,"usage: gkd-menu-config check FILE | match FILE OPK_SHA256 DESKTOP EXEC [ROM]\n");return 64;
    }
    struct gkd_menu_config *c=malloc(sizeof(*c));unsigned line=0;
    if(!c)return 70;
    if(gkd_menu_config_load(argv[2],c,&line)){
        fprintf(stderr,"GKD_MENU_CONFIG=INVALID line=%u errno=%d\n",line,errno);free(c);return 65;
    }
    if(!match){printf("GKD_MENU_CONFIG=VALID version=%u profiles=%u\n",c->version,c->count);free(c);return 0;}
    struct gkd_menu_profile p;int found=gkd_menu_config_select_game(c,argv[3],argv[4],argv[5],argc-6,argv+6,&p);
    free(c);
    if(found<0){fprintf(stderr,"GKD_MENU_CONFIG=INVALID_IDENTITY errno=%d\n",errno);return 65;}
    if(!found){puts("GKD_MENU_CONFIG=UNKNOWN");return 2;}
    printf("GKD_MENU_CONFIG=MATCH id=%s action=%s hold_ms=%u keys=",p.id,gkd_menu_action_name(p.action),p.action==GKD_MENU_CHORD?p.hold_ms:0U);
    for(unsigned i=0;i<p.key_count;i++)printf("%s%u",i?"+":"",p.keys[i]);
    putchar('\n');return 0;
}
