/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-profile.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
static void file(const char *path){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0400);assert(fd>=0);close(fd);}
int main(void)
{
 assert(!mkdir("/tmp/gkd-profile-cleanup",0700));
 assert(!mkdir("/tmp/gkd-profile-cleanup/gkd-app-profile.123",0700));
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/inittab");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/libgkd-sm-present.so");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/libgkd-fps-present.so");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/child-output.log");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/gkd-app-game");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/loop-owner");
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/input-routing.conf");
 assert(!mkdir("/tmp/gkd-profile-cleanup/gkd-app-profile.123/input-config",0700));
 file("/tmp/gkd-profile-cleanup/gkd-app-profile.123/foreign");
 assert(gkd_app_profile_cleanup(123)<0&&errno==EPERM);
 assert(!access("/tmp/gkd-profile-cleanup/gkd-app-profile.123/inittab",F_OK));
 assert(!unlink("/tmp/gkd-profile-cleanup/gkd-app-profile.123/foreign"));
 assert(!gkd_app_profile_cleanup(123));
 assert(!symlink("/tmp","/tmp/gkd-profile-cleanup/gkd-app-profile.124"));
 assert(gkd_app_profile_cleanup(124)<0);
 assert(!unlink("/tmp/gkd-profile-cleanup/gkd-app-profile.124"));
 assert(!mkdir("/tmp/gkd-profile-cleanup/gkd-app-profile.125",0700));
 assert(!symlink("/etc/passwd","/tmp/gkd-profile-cleanup/gkd-app-profile.125/inittab"));
 assert(gkd_app_profile_cleanup(125)<0&&errno==EPERM);
 assert(!unlink("/tmp/gkd-profile-cleanup/gkd-app-profile.125/inittab"));assert(!gkd_app_profile_cleanup(125));
 assert(!rmdir("/tmp/gkd-profile-cleanup"));
 puts("GKD_APP_PROFILE_CLEANUP=PASS exact-files/no-foreign/no-symlink/partial");
 return 0;
}
