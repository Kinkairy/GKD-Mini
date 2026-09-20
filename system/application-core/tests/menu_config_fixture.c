/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-menu-config.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define HASH "f39b6a9927e4b4428ba63c0b9e1a79917f1d1a680f454cea0f3a7e5e68ad34ef"
static const char prefix[]="version=1\n[pocket]\nopk_sha256=" HASH "\ndesktop=default.gcw0.desktop\nexec=PocketSNES\n";
static unsigned checks;
static struct gkd_menu_config *actual,*saved;
static void parse(const char *s,int valid)
{
    unsigned line=999;memcpy(saved,actual,sizeof(*saved));
    int rc=gkd_menu_config_parse(s,strlen(s),actual,&line);
    if(valid){assert(!rc);assert(!line);}
    else {assert(rc==-1&&errno==EPROTO);assert(!memcmp(actual,saved,sizeof(*actual)));}
    ++checks;
}
static void suffix(const char *s,int valid)
{char buffer[4096];snprintf(buffer,sizeof(buffer),"%s%s",prefix,s);parse(buffer,valid);}
int main(int argc,char **argv)
{
    assert(argc==2);actual=calloc(1,sizeof(*actual));saved=malloc(sizeof(*saved));assert(actual&&saved);
    suffix("action=chord\nkeys=KEY_ESC+KEY_ENTER\n",1);
    assert(actual->count==1&&actual->profiles[0].hold_ms==100&&actual->profiles[0].key_count==2);
    assert(actual->profiles[0].keys[0]==1&&actual->profiles[0].keys[1]==28);
    struct gkd_menu_profile selected;
    assert(gkd_menu_config_select(actual,HASH,"default.gcw0.desktop","PocketSNES",&selected)==1);
    selected.hold_ms=500;assert(actual->profiles[0].hold_ms==100); /* session copy */
    assert(!gkd_menu_config_select(actual,HASH,"other.desktop","PocketSNES",&selected));
    assert(!selected.id[0]&&selected.action==GKD_MENU_DISABLED&&!selected.key_count);
    assert(!gkd_menu_config_select(actual,HASH,"default.gcw0.desktop","other",&selected));
    assert(gkd_menu_config_select(actual,"*","default.gcw0.desktop","PocketSNES",&selected)==-1);
    checks+=4;
    const char *bad[]={
      "action=chord\n", "action=chord\nkeys=\n", "action=chord\nkeys=KEY_ESC+\n",
      "action=chord\nkeys=KEY_ESC+KEY_ESC\n", "action=chord\nkeys=KEY_UNKNOWN\n",
      "action=chord\nkeys=0\n", "action=chord\nkeys=768\n", "action=chord\nkeys=-1\n",
      "action=chord\nkeys=1+2+3+4+5\n", "action=chord\nkeys=KEY_ESC\nhold_ms=19\n",
      "action=chord\nkeys=KEY_ESC\nhold_ms=501\n", "action=chord\nkeys=KEY_ESC\nhold_ms=1000000000000000000\n",
      "action=native\nkeys=KEY_ESC\n", "action=none\nhold_ms=100\n", "action=disabled\nkeys=KEY_ESC\n",
      "action=native\naction=native\n", "action=native\nexec=other\n", "action=native\ncommand=reboot\n",
      "action=native # unsupported inline comment\n", "action=unknown\n", "action=native\nversion=1\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)suffix(bad[i],0);
    suffix("action=chord\nkeys=59+60\nhold_ms=20\n",1); /* future F1/F2, no code change */
    suffix("action=chord\nkeys=KEY_TAB + KEY_BACKSPACE\nhold_ms=500\n",1);
    suffix("action=native\n",1);suffix("action=disabled\n",1);suffix("action=none\n",1);
    parse("version=1\n",1);assert(!actual->count);
    parse("version=3\n",0);parse("version=1\nversion=1\n",0);parse("[x]\naction=native\n",0);
    parse("version=1\n[bad id]\n",0);parse("version=1\n[x]\n",0);
    char text[4096];snprintf(text,sizeof(text),"%saction=native\n%saction=native\n",prefix,prefix+10);
    parse(text,0); /* duplicate ID and exact identity */
    char *second=strstr(text+strlen(prefix),"[pocket]");assert(second);memcpy(second,"[other_]",8);
    parse(text,0); /* different ID, same match tuple */
    snprintf(text,sizeof(text),"%saction=native\n",prefix);
    char *entry=strstr(text,"exec=PocketSNES");assert(entry);memcpy(entry+5,"../bad     ",11);parse(text,0);
    const char nul[]="version=1\n\0ignored";
    memcpy(saved,actual,sizeof(*saved));assert(gkd_menu_config_parse(nul,sizeof(nul)-1,actual,NULL)==-1);
    assert(!memcmp(saved,actual,sizeof(*saved)));++checks;
    char crlf[4096];size_t n=0;
    snprintf(text,sizeof(text),"%saction=chord\nkeys=KEY_ESC+KEY_ENTER\n",prefix);
    for(size_t i=0;text[i];i++){if(text[i]=='\n')crlf[n++]='\r';crlf[n++]=text[i];}crlf[n]=0;parse(crlf,1);
    memcpy(saved,actual,sizeof(*saved));
    assert(gkd_menu_config_parse("x",GKD_MENU_CONFIG_BYTES+1U,actual,NULL)==-1);
    assert(!memcmp(saved,actual,sizeof(*saved)));++checks;
    /* Every truncation must either produce a fully valid configuration or
     * leave the previous configuration intact. Sanitizers check all offsets. */
    for(size_t i=0;i<strlen(text);i++){
        memcpy(saved,actual,sizeof(*saved));
        if(gkd_menu_config_parse(text,i,actual,NULL))assert(!memcmp(saved,actual,sizeof(*saved)));
    }
    /* Version 2: exact game identity, physical aliases and frozen schemes. */
    const char v2[]="version=2\n[core]\nopk_sha256=" HASH
      "\ndesktop=default.gcw0.desktop\nexec=PocketSNES\naction=native\nmap.l1=59\n"
      "[game]\nopk_sha256=" HASH "\ndesktop=default.gcw0.desktop\nexec=PocketSNES\naction=native\n"
      "rom=/media/sdcard/roms/Beat Em Up.sfc\nmap.side_dot=KEY_SPACE\n";
    parse(v2,1);
    char *game[]={"/media/sdcard/roms/Beat Em Up.sfc"};
    assert(gkd_menu_config_select_game(actual,HASH,"default.gcw0.desktop","PocketSNES",1,game,&selected)==1);
    assert(!strcmp(selected.id,"game")&&selected.map_count==2);
    struct gkd_menu_vt_config route;
    assert(!gkd_input_route_compile(0,102,&selected,&route)&&!route.version&&!route.map_count);
    assert(gkd_input_route_compile(1,102,&selected,&route)==1&&route.count==1&&route.keys[0]==102);
    unsigned dot=0,inherited=0;
    for(unsigned i=0;i<route.map_count;i++) {
     if(route.maps[i].source==29)dot=route.maps[i].target;
     if(route.maps[i].source==15)inherited=route.maps[i].target;
    }
    assert(dot==57&&inherited==59);
    struct gkd_menu_vt_config xbox=route;
    assert(gkd_input_route_compile(2,102,&selected,&route)==1&&!memcmp(&route,&xbox,sizeof(route)));
    assert(gkd_input_route_compile(3,102,&selected,&route)<0);
    assert(gkd_menu_config_select_game(actual,HASH,"default.gcw0.desktop","PocketSNES",0,NULL,&selected)==1);
    assert(!strcmp(selected.id,"core")&&selected.map_count==1);
    char invalid_v2[4096];snprintf(invalid_v2,sizeof(invalid_v2),"%smap.a=KEY_ENTER\n",v2);parse(invalid_v2,0);
    snprintf(invalid_v2,sizeof(invalid_v2),"%smap.side_double_dot=KEY_F1+KEY_F2\n",v2);parse(invalid_v2,0);
    snprintf(invalid_v2,sizeof(invalid_v2),"%smap.unknown=KEY_ENTER\n",v2);parse(invalid_v2,0);
    snprintf(invalid_v2,sizeof(invalid_v2),"%srom=/media/sdcard/../other\n",v2);parse(invalid_v2,0);
    puts("GKD_INPUT_CONFIG_TEST=PASS raw/xbox/ps/game-path/inheritance/side-dot-alias/conflict/invalid");
    /* Real file loader and complete audited catalog. */
    assert(!gkd_menu_config_load(argv[1],actual,NULL));assert(actual->count==28);
    unsigned counts[4]={0};for(unsigned i=0;i<actual->count;i++)counts[actual->profiles[i].action]++;
    assert(counts[GKD_MENU_NATIVE]==18&&counts[GKD_MENU_CHORD]==7&&counts[GKD_MENU_DISABLED]==2&&counts[GKD_MENU_NONE]==1);
    assert(gkd_menu_config_select_game(actual,"a97a8775d905bc4d6dca69dd907751537a6f36ce329bed74b5c73f96b9876377",
      "default.gcw0.desktop","gen_mini",0,NULL,&selected)==1);
    assert(!strcmp(selected.id,"genesis-sx-menu-fixed")&&selected.action==GKD_MENU_NATIVE);
    char name[]="/out/config-fixture-XXXXXX";int fd=mkstemp(name);assert(fd>=0);
    assert(write(fd,text,strlen(text))==(ssize_t)strlen(text));close(fd);
    assert(!gkd_menu_config_load(name,actual,NULL));++checks;
    assert(!chmod(name,0666));assert(gkd_menu_config_load(name,actual,NULL)==-1&&errno==EPERM);++checks;
    assert(!chmod(name,0600));assert(!chown(name,1234,1234));
    assert(gkd_menu_config_load(name,actual,NULL)==-1&&errno==EPERM);++checks;
    assert(!unlink(name));assert(!symlink(argv[1],name));assert(gkd_menu_config_load(name,actual,NULL)==-1);++checks;
    assert(!unlink(name));assert(!mkfifo(name,0600));assert(gkd_menu_config_load(name,actual,NULL)==-1&&errno==EPERM);++checks;
    assert(!unlink(name));assert(gkd_menu_config_load(name,actual,NULL)==-1&&errno==ENOENT);++checks;
    printf("GKD_MENU_CONFIG_TEST=PASS checks=%u catalog=28 truncation_sweep=PASS\n",checks);
    free(actual);free(saved);return 0;
}
