/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-fps-counter.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern int SDL_Flip(void *);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fixture line=%d errno=%d\n", __LINE__, errno); return 9; } } while (0)
static int environment_ok(void)
{
    const char *expected = getenv("GKD_TEST_EXPECT_PRELOAD");
    const char *actual = getenv("LD_PRELOAD");
    if (!expected) return 0;
    if (*expected ? (!actual || strcmp(actual, expected)) : actual != NULL) return 0;
    return !getenv(GKD_FPS_COUNTER_FD_ENV) &&
           !getenv(GKD_FPS_LIFETIME_FD_ENV) && !getenv(GKD_FPS_SESSION_ENV) &&
           !getenv(GKD_FPS_PRELOAD_PATH_ENV);
}
static int checked_flip(int expected)
{
    int value = expected;
    errno = 41;
    int got = SDL_Flip(&value);
    return got == expected && errno == 97;
}
static void *thread_main(void *opaque)
{
    unsigned int count = *(unsigned int *)opaque;
    while (count--) if (!checked_flip(0)) return (void *)1;
    return NULL;
}
int main(int argc, char **argv)
{
    CHECK(argc >= 2 && environment_ok());
    if (!strcmp(argv[1], "sequence")) {
        CHECK(checked_flip(0)); CHECK(checked_flip(-1)); CHECK(checked_flip(0));
        CHECK(checked_flip(-1)); CHECK(checked_flip(0));
    } else if (!strcmp(argv[1], "successes")) {
        unsigned long count = argc == 3 ? strtoul(argv[2], NULL, 10) : 0;
        while (count--) CHECK(checked_flip(0));
    } else if (!strcmp(argv[1], "threads")) {
        enum { THREADS = 8 }; pthread_t threads[THREADS]; void *value;
        unsigned int each = 10000U;
        for (unsigned int i = 0; i < THREADS; ++i) CHECK(!pthread_create(&threads[i], NULL, thread_main, &each));
        for (unsigned int i = 0; i < THREADS; ++i) { CHECK(!pthread_join(threads[i], &value)); CHECK(!value); }
    } else if (!strcmp(argv[1], "fork")) {
        pid_t child = fork(); int status;
        CHECK(child >= 0);
        if (!child) _exit(checked_flip(0) ? 0 : 9);
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        CHECK(checked_flip(0));
    } else if (!strcmp(argv[1], "exec")) {
        pid_t child = fork(); int status;
        CHECK(child >= 0);
        if (!child) { execl(argv[0], argv[0], "exec-child", NULL); _exit(9); }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        CHECK(checked_flip(0));
    } else if (!strcmp(argv[1], "exec-child")) {
        CHECK(checked_flip(0));
    } else if (!strcmp(argv[1], "replace")) {
        const char *replacement = getenv("GKD_TEST_REPLACE");
        CHECK(replacement && *replacement);
        execl(replacement, replacement, "hold", NULL);
        return 9;
    } else return 8;
    return 0;
}
