/* SPDX-License-Identifier: GPL-2.0 */
/* Atomic symlink replacement for BusyBox versions without mv -T. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    struct stat source, target;
    if (argc != 3) return 64;
    if (lstat(argv[1], &source) || !S_ISLNK(source.st_mode)) return 1;
    if (!lstat(argv[2], &target)) {
        if (!S_ISLNK(target.st_mode)) return 1;
    } else if (errno != ENOENT) return 1;
    return rename(argv[1], argv[2]) ? 1 : 0;
}
