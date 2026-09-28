/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-game-orientation.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
unsigned int nBurnDrvActive=0,nBurnDrvCount=3;
static unsigned flags=4;
static char *driver="truxton";
unsigned int BurnDrvGetFlags(void){return flags;}
char *BurnDrvGetTextA(unsigned int which){assert(!which);return driver;}
int SDL_Flip(void *);
int main(void)
{
    int observer=atoi(getenv("GKD_TEST_OBSERVER_FD")),success=0,failed=-1;
    struct gkd_orientation_page *p=mmap(NULL,sizeof(*p),PROT_READ,MAP_SHARED,observer,0);
    assert(p!=MAP_FAILED);close(observer);
    assert(!getenv(GKD_ORIENTATION_FD_ENV));
    assert(SDL_Flip(&success)==0&&errno==97&&p->aspect==2);
    flags=0;assert(SDL_Flip(&failed)==-1&&p->aspect==2);
    assert(SDL_Flip(&success)==0&&p->aspect==1);
    driver="other";assert(SDL_Flip(&success)==0&&!p->aspect);
    driver="truxton";nBurnDrvActive=3;assert(SDL_Flip(&success)==0&&!p->aspect);
    nBurnDrvActive=0;flags=4;assert(SDL_Flip(&success)==0&&p->aspect==2);
    pid_t child=fork();assert(child>=0);
    if(!child){flags=0;assert(SDL_Flip(&success)==0);exit(0);}
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
    assert(p->aspect==2); /* Neither fork's frames nor its destructor publish. */
    puts("GKD_ORIENTATION_PRESENT=PASS portrait landscape failed-flip wrong-driver invalid-index fork");
    munmap(p,sizeof(*p));return 0;
}
