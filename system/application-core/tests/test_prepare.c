/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-prepare.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static unsigned descriptors(void)
{
    DIR *d = opendir("/proc/self/fd");
    assert(d);
    unsigned count = 0;
    while (readdir(d)) ++count;
    assert(!closedir(d));
    return count;
}
int main(void)
{
    struct gkd_app_prepare_request request = {0};
    struct sigaction before = {0}, after;
    sigset_t mask;
    assert(getpid() != 1);
    before.sa_handler = SIG_IGN;
    assert(!sigemptyset(&before.sa_mask));
    assert(!sigaction(SIGPIPE, &before, NULL));
    assert(!sigemptyset(&mask));
    assert(!sigaddset(&mask, SIGUSR1));
    assert(!sigprocmask(SIG_BLOCK, &mask, NULL));
    unsigned initial = descriptors();
    errno = 0;
    assert(gkd_app_prepare_replace(NULL) == -1 && errno == EINVAL);
    errno = 0;
    assert(gkd_app_prepare_replace(&request) == -1 && errno == EPERM);
    assert(descriptors() == initial);
    assert(!sigaction(SIGPIPE, NULL, &after) && after.sa_handler == SIG_IGN);
    assert(!sigprocmask(SIG_SETMASK, NULL, &mask) && sigismember(&mask, SIGUSR1) == 1);
    puts("PASS prepare wrong-context refusal: descriptors and signals unchanged (1 case)");
    return 0;
}
