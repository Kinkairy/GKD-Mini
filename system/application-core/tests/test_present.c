/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-ready.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "present line=%d errno=%d\n", __LINE__, errno); exit(1); } } while (0)
static const unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static const char hex[] = "0102030405060708090a0b0c0d0e0f10";
int main(void)
{
    for (unsigned int kind = 0; kind < 10; ++kind) {
        int pair[2], status;
        char number[32], byte;
        struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
        CHECK(gkd_app_ready_channel(pair) == 0);
        if (kind == 7) {
            unsigned int n;
            for (n = 0; n < 100000 && gkd_app_ready_send(pair[1], token) == 0; ++n) {}
            CHECK(n < 100000 && (errno == EAGAIN || errno == EWOULDBLOCK));
        }
        pid_t child = fork(); CHECK(child >= 0);
        if (!child) {
            (void)close(pair[0]);
            CHECK(fcntl(pair[1], F_SETFD, 0) == 0);
            snprintf(number, sizeof(number), "%d", pair[1]);
            CHECK(setenv("LD_PRELOAD", "/test/libgkd-sm-present.so", 1) == 0);
            CHECK(setenv("GKD_APP_READY_FD", number, 1) == 0);
            CHECK(setenv("GKD_APP_READY_TOKEN", hex, 1) == 0);
            if (kind == 2) CHECK(unsetenv("GKD_APP_READY_TOKEN") == 0);
            if (kind == 3) CHECK(setenv("GKD_APP_READY_TOKEN", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 1) == 0);
            if (kind == 4) CHECK(setenv("GKD_APP_READY_FD", "999999999999999999999", 1) == 0);
            if (kind == 5) {
                CHECK(close(pair[1]) == 0);
                int plain = open("/dev/null", O_WRONLY);
                CHECK(plain >= 0);
                snprintf(number, sizeof(number), "%d", plain);
                CHECK(setenv("GKD_APP_READY_FD", number, 1) == 0);
            }
            if (kind == 8) CHECK(setenv("LD_PRELOAD", "/test/libgkd-sm-present.so:/test/libfake-sdl.so", 1) == 0);
            if (kind == 9) {
                int disconnected[2];
                CHECK(close(pair[1]) == 0);
                CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, disconnected) == 0);
                CHECK(close(disconnected[0]) == 0);
                snprintf(number, sizeof(number), "%d", disconnected[1]);
                CHECK(setenv("GKD_APP_READY_FD", number, 1) == 0);
            }
            execl("/test/present-fixture", "present-fixture",
                  kind == 1 ? "failed-only" : kind == 6 ? "descendants" : "normal", number, (char *)0);
            _exit(9);
        }
        CHECK(gkd_app_ready_init(&gate, pair[0], child, getuid(), token, 100, 100) == 0);
        CHECK(close(pair[1]) == 0);
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        /* Validate transport credentials here, not real launcher liveness. */
        if (kind == 0 || kind == 6) {
            CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_FRAME_SUBMITTED);
            CHECK(recv(pair[0], &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);
        } else if (kind == 7) {
            /* Filled by parent: not an authenticated child event. */
            CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_BAD_EVENT);
        } else {
            CHECK(gkd_app_ready_poll(&gate, 101, 1) == GKD_APP_WAITING);
        }
        gkd_app_ready_close(&gate);
    }
    puts("GKD_SM_PRESENT=PASS cases=10 forwarding=PASS fork-exec-isolation=PASS");
    return 0;
}
