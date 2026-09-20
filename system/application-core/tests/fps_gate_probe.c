/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-app-fps-gate.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv)
{
    if(argc!=9)return 64;
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);
    int directory=open(argv[2],O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0||directory<0)return 65;
    struct gkd_app_fps_gate_paths p={argv[4],argv[5],argv[6],argv[7],argv[8]};
    int result=gkd_app_fps_gate(fd,directory,strcmp(argv[3],"-")?argv[3]:NULL,&p);
    close(fd);close(directory);
    printf("%d\n",result);
    return result<0?66:0;
}
