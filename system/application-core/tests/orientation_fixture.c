/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-orientation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc,char **argv)
{
    if(argc==3){struct gkd_game_orientation s;gkd_app_orientation_rom(argv[2],atoi(argv[1]),&s);
        assert(gkd_game_orientation_valid(&s));gkd_game_orientation_print(&s,1);return 0;}
    struct gkd_app_orientation session=GKD_APP_ORIENTATION_INIT;
    struct gkd_game_orientation state;
    struct gkd_menu_profile profile={0};
    strcpy(profile.opk_sha256,"ac721a8bd00a5ab0731a461240b79597cdf8c52b25d2cca7f011e6d5a795d691");
    strcpy(profile.desktop,"fba_ux.gcw0.desktop");strcpy(profile.executable,"fbasdl.dge");
    char *rom[]={"/roms/truxton.zip"};
    gkd_app_orientation_prepare(&session,&profile,1,rom);
    assert(session.fd>=3&&session.page&&!strcmp(session.page->driver,"truxton"));
    int life[2];assert(!pipe(life));session.lifetime=life[0];
    gkd_app_orientation_read(&session,&state);assert(!state.aspect);
    session.page->aspect=GKD_ASPECT_PORTRAIT;
    gkd_app_orientation_read(&session,&state);assert(state.aspect==2&&state.scope==2&&gkd_game_orientation_valid(&state));
    close(life[1]); /* exec/crash invalidates a stale nonzero page. */
    gkd_app_orientation_read(&session,&state);assert(!state.aspect);
    gkd_app_orientation_close(&session);gkd_app_orientation_read(&session,&state);assert(!state.aspect);
    profile.opk_sha256[0]='0';gkd_app_orientation_prepare(&session,&profile,1,rom);
    assert(session.fd==-1&&!session.page&&!session.launch.aspect);
    state.version=99;assert(!gkd_game_orientation_valid(&state));
    puts("GKD_ORIENTATION_SESSION=PASS valid-identity invalid-identity producer-death close reset");
    return 0;
}
