/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
extern int SDL_Flip(void *surface);
#define CHECK(x) do { if (!(x)) return 91; } while (0)
int main(int argc, char **argv)
{
    CHECK(!getenv("LD_PRELOAD") && !getenv("GKD_APP_READY_FD") && !getenv("GKD_APP_READY_TOKEN"));
    CHECK(getenv("HOME") && !strcmp(getenv("HOME"), "/fixture-home"));
    struct stat input, output, error;
    CHECK(!fstat(0, &input) && S_ISCHR(input.st_mode));
    CHECK(!fstat(1, &output) && S_ISFIFO(output.st_mode));
    CHECK(!fstat(2, &error) && S_ISFIFO(error.st_mode));
    CHECK(output.st_dev == error.st_dev && output.st_ino == error.st_ino);
    CHECK(write(1, "stdout\n", 7) == 7 && write(2, "stderr\n", 7) == 7);
    CHECK(fcntl(3, F_GETFD) & FD_CLOEXEC);
    CHECK(fcntl(4, F_GETFD) < 0 && errno == EBADF);
    CHECK(fcntl(4096, F_GETFD) < 0 && errno == EBADF);
    DIR *dir = opendir("/proc/self/fd");
    CHECK(dir != NULL);
    unsigned int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    CHECK(closedir(dir) == 0 && count == 5); /* 0..3 plus directory handle */
    int result = 0;
    errno = 41; CHECK(SDL_Flip(&result) == 0 && errno == 97);
    CHECK(fcntl(3, F_GETFD) < 0 && errno == EBADF);
    errno = 41; CHECK(SDL_Flip(&result) == 0 && errno == 97);
    if (argc == 2 && !strcmp(argv[1], "--watch-hold")) usleep(500000);
    return 0;
}
