/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#ifdef FIXTURE_SDL
int SDL_Flip(void *surface)
{
    if (errno != 41) return -101;
    errno = 97;
    return *(int *)surface;
}
#else
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern int SDL_Flip(void *surface);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fixture line=%d\n", __LINE__); return 9; } } while (0)
static int flip(int value)
{
    errno = 41;
    int result = SDL_Flip(&value);
    return result == value && errno == 97;
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    int fd = atoi(argv[2]), status;
    CHECK(!getenv("LD_PRELOAD") && !getenv("GKD_APP_READY_FD") && !getenv("GKD_APP_READY_TOKEN"));
    if (!strcmp(argv[1], "exec-child")) {
        CHECK(fcntl(fd, F_GETFD) < 0 && errno == EBADF);
        CHECK(flip(0));
        return 0;
    }
    if (!strcmp(argv[1], "descendants")) {
        CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) _exit(flip(0) ? 0 : 9);
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        child = fork(); CHECK(child >= 0);
        if (!child) { execl("/test/present-fixture", "present-fixture", "exec-child", argv[2], (char *)0); _exit(9); }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    CHECK(flip(-1));
    if (strcmp(argv[1], "failed-only")) CHECK(flip(0) && flip(0));
    return 0;
}
#endif
