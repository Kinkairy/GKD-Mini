/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Private launch protocol, not a second user configuration parser.
 * The launcher owns this child's SINGLE-entry LD_PRELOAD environment and
 * passes only its dedicated event endpoint, never management descriptors.
 */
static int event_fd = -1;
static unsigned char launch_token[GKD_APP_READY_TOKEN_BYTES];
static pid_t launch_pid;
static int attempted;
static int (*original_flip)(void *);

static int endpoint_number(const char *value)
{
    unsigned int number = 0;
    if (!value || !*value) return -1;
    for (; *value; ++value) {
        if (*value < '0' || *value > '9' ||
            number > ((unsigned int)INT_MAX - (unsigned int)(*value - '0')) / 10)
            return -1;
        number = number * 10 + (unsigned int)(*value - '0');
    }
    return number >= 3 ? (int)number : -1;
}

static int token_hex(const char *value)
{
    unsigned int i, any = 0;
    if (!value || strnlen(value, 2 * sizeof(launch_token) + 1) != 2 * sizeof(launch_token)) return 0;
    for (i = 0; i < sizeof(launch_token); ++i) {
        unsigned int byte = 0, n;
        for (n = 0; n < 2; ++n) {
            unsigned char c = (unsigned char)*value++;
            unsigned int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return 0;
            byte = byte * 16 + digit;
        }
        launch_token[i] = (unsigned char)byte; any |= byte;
    }
    return any != 0;
}

static int event_socket(int fd)
{
    struct sockaddr_storage address;
    int type;
    socklen_t length = sizeof(address), type_length = sizeof(type);
    return fd >= 3 && !getsockname(fd, (struct sockaddr *)&address, &length) &&
        address.ss_family == AF_UNIX &&
        !getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) && type == SOCK_DGRAM;
}

__attribute__((constructor)) static void present_init(void)
{
    int saved = errno;
    int fd = endpoint_number(getenv("GKD_APP_READY_FD"));
    int token_ok = token_hex(getenv("GKD_APP_READY_TOKEN"));
    const char *preload = getenv("LD_PRELOAD");
    Dl_info self;
    int single_self = dladdr((void *)present_init, &self) && preload &&
        self.dli_fname && !strcmp(preload, self.dli_fname);
    /* Called before SM main/threads. The launch contract owns all three
     * variables. No process-global preload file is installed or modified.
     * Even malformed launch input cannot propagate this preload via exec.
     */
    int env_ok = unsetenv("LD_PRELOAD") == 0;
    if (unsetenv("GKD_APP_READY_FD")) env_ok = 0;
    if (unsetenv("GKD_APP_READY_TOKEN")) env_ok = 0;
    original_flip = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_Flip");
    launch_pid = getpid();
    if (event_socket(fd)) {
        int flags = fcntl(fd, F_GETFD);
        if (flags >= 0 && !fcntl(fd, F_SETFD, flags | FD_CLOEXEC) &&
            token_ok && single_self && env_ok) event_fd = fd;
        else (void)close(fd);
    }
    if (event_fd < 0) memset(launch_token, 0, sizeof(launch_token));
    errno = saved;
}

__attribute__((visibility("default"))) int SDL_Flip(void *surface)
{
    int result, saved;
    if (!original_flip) { errno = ENOSYS; return -1; }
    result = original_flip(surface);
    saved = errno;
    if (result == 0 && !__sync_lock_test_and_set(&attempted, 1)) {
        /* Fork-only descendants must not send with inherited state. Exec
         * descendants have neither this preload nor the CLOEXEC endpoint.
         */
        if (event_fd >= 0) {
            if (getpid() == launch_pid)
                (void)gkd_app_ready_send(event_fd, launch_token);
            (void)close(event_fd);
            event_fd = -1;
        }
        memset(launch_token, 0, sizeof(launch_token));
    }
    errno = saved;
    return result;
}
