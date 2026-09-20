/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-exec.h"
#include "gkd-app-ready.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "report line=%d errno=%d\n", __LINE__, errno); exit(1); } } while (0)
static const unsigned char token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static int descriptors(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    CHECK(dir);
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    CHECK(closedir(dir) == 0);
    return count;
}
int main(void)
{
    static const char *names[] = {"success", "validation-error", "execveat-error",
        "queue-full", "write-shutdown", "regular-file", "no-cloexec", "blocking",
        "datagram", "aliased-ready", "closed-fd", "slot5", "slot3", "slot4096",
        "slot5-exec-error", "stage-emfile", "negative-fd", "null-request"};
    int initial = descriptors();
    struct rlimit limit;
    CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_max > 4096);
    if (limit.rlim_cur <= 4096) { limit.rlim_cur = 4097; CHECK(setrlimit(RLIMIT_NOFILE, &limit) == 0); }
    int executable = open("/test/exec-fixture", O_RDONLY | O_CLOEXEC);
    int terminal = open("/dev/null", O_RDWR | O_CLOEXEC);
    int noise = open("/test/report-parent-owned", O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK | O_CLOEXEC, 0600);
    CHECK(executable >= 3 && terminal >= 3 && noise >= 3);
    char *argv[] = {"exec-fixture", NULL};
    char *environment[] = {"HOME=/fixture-home", "PATH=/bin", NULL};
    for (unsigned int kind = 0; kind < sizeof(names)/sizeof(names[0]); ++kind) {
        int pair[2], errors[2], output[2], status;
        unsigned char packet[32];
        struct gkd_app_ready_gate gate = GKD_APP_READY_GATE_INIT;
        CHECK(gkd_app_ready_channel(pair) == 0);
        CHECK(pipe2(output, O_NONBLOCK | O_CLOEXEC) == 0);
        CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, errors) == 0);
        struct gkd_app_exec_request request = {getpid(), executable, terminal, output[1], pair[1],
            "/test/libgkd-sm-present.so", token, argv, environment};
        CHECK(gkd_app_exec_replace_report(&request, errors[1]) == -1 && errno == EPERM);
        CHECK(recv(errors[0], packet, sizeof(packet), MSG_DONTWAIT) == -1 && errno == EAGAIN);
        if (kind == 3) {
            while (send(errors[1], "full", 4, MSG_DONTWAIT | MSG_NOSIGNAL) == 4) {}
            CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
        }
        pid_t child = fork(); CHECK(child >= 0);
        if (!child) {
            int fd = errors[1], wanted = EINVAL, result, error;
            char *bad_env[] = {"LD_PRELOAD=/wrong", NULL};
            CHECK(signal(SIGPIPE, SIG_DFL) != SIG_ERR);
            CHECK(close(errors[0]) == 0);
            if (kind == 1 || kind == 3 || kind == 4) request.base_env = bad_env;
            if (kind == 2 || kind == 14) {
                int invalid = memfd_create("report-invalid-elf", MFD_CLOEXEC);
                CHECK(invalid >= 3 && write(invalid, "\177ELF", 4) == 4);
                request.executable_fd = invalid;
                wanted = ENOEXEC;
            }
            if (kind == 4) CHECK(shutdown(fd, SHUT_WR) == 0);
            if (kind == 5) { fd = noise; wanted = ENOTSOCK; }
            if (kind == 6) CHECK(fcntl(fd, F_SETFD, 0) == 0);
            if (kind == 7) CHECK(fcntl(fd, F_SETFL, 0) == 0);
            if (kind == 8) {
                int datagram[2]; CHECK(gkd_app_ready_channel(datagram) == 0);
                fd = datagram[1];
            }
            if (kind == 9) fd = pair[1];
            if (kind == 10) { CHECK(close(fd) == 0); wanted = EBADF; }
            if (kind == 11 || kind == 12 || kind == 13 || kind == 14) {
                /* Preserve all inputs before deliberately colliding with low slots. */
                request.executable_fd = fcntl(request.executable_fd, F_DUPFD_CLOEXEC, 20);
                request.terminal_fd = fcntl(request.terminal_fd, F_DUPFD_CLOEXEC, 20);
                request.ready_fd = fcntl(request.ready_fd, F_DUPFD_CLOEXEC, 20);
                CHECK(request.executable_fd >= 20 && request.terminal_fd >= 20 && request.ready_fd >= 20);
                int target = kind == 12 ? 3 : (kind == 13 ? 4096 : 5);
                CHECK(dup3(fd, target, O_CLOEXEC) == target);
                fd = target;
            }
            if (kind == 15) {
                struct rlimit tight = {64, 64};
                CHECK(setrlimit(RLIMIT_NOFILE, &tight) == 0);
                while (fcntl(noise, F_DUPFD_CLOEXEC, 8) >= 0) {}
                CHECK(errno == EMFILE); wanted = EMFILE;
            }
            if (kind == 16) fd = -1;
            if (kind == 17) wanted = EPERM;
            result = gkd_app_exec_replace_report(kind == 17 ? NULL : &request, fd);
            error = errno;
            if (kind == 0 || kind == 11 || kind == 12 || kind == 13) _exit(92);
            if (kind != 2 && kind != 14) CHECK(fcntl(noise, F_GETFD) >= 0);
            _exit(result == -1 && error == wanted ? 0 : 93);
        }
        CHECK(close(errors[1]) == 0 && close(pair[1]) == 0 && close(output[1]) == 0);
        CHECK(gkd_app_ready_init(&gate, pair[0], child, getuid(), token, 100, 100) == 0);
        CHECK(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "report case=%s status=%d\n", names[kind], status); return 1;
        }
        if (kind == 1 || kind == 2 || kind == 14 || kind == 15) {
            unsigned int wanted = kind == 1 ? EINVAL : (kind == 15 ? EMFILE : ENOEXEC);
            CHECK(recv(errors[0], packet, sizeof(packet), MSG_DONTWAIT) == GKD_APP_EXEC_ERROR_BYTES);
            CHECK(!memcmp(packet, "GKDEXE1\0", 8));
            CHECK((((unsigned int)packet[8] << 24) | ((unsigned int)packet[9] << 16) |
                   ((unsigned int)packet[10] << 8) | packet[11]) == wanted);
        }
        if (kind == 2) {
            char diagnostic[256]; int reported, release_state;
            unsigned long long monotonic;
            ssize_t length = read(output[0], diagnostic, sizeof(diagnostic) - 1);
            CHECK(length > 0 && (size_t)length < sizeof(diagnostic));
            diagnostic[length] = 0;
            CHECK(diagnostic[length - 1] == '\n' &&
                  sscanf(diagnostic, "GKD_APP phase=EXEC_FAILURE errno=%d mono_ms=%llu release_state=%d\n",
                         &reported, &monotonic, &release_state) == 3 && reported == ENOEXEC);
        }
        if (kind == 3) {
            ssize_t n;
            while ((n = recv(errors[0], packet, sizeof(packet), MSG_DONTWAIT)) > 0)
                CHECK(n == 4 && !memcmp(packet, "full", 4));
            CHECK(n == 0);
        } else CHECK(recv(errors[0], packet, sizeof(packet), MSG_DONTWAIT) == 0);
        CHECK(gkd_app_ready_poll(&gate, 101, 1) ==
            (kind == 0 || kind == 11 || kind == 12 || kind == 13 ? GKD_APP_FRAME_SUBMITTED : GKD_APP_WAITING));
        gkd_app_ready_close(&gate);
        CHECK(close(errors[0]) == 0);
        CHECK(close(output[0]) == 0);
        struct stat st;
        CHECK(fstat(noise, &st) == 0 && st.st_size == 0);
        CHECK(fcntl(executable, F_GETFD) >= 0 && fcntl(terminal, F_GETFD) >= 0);
    }
    CHECK(close(executable) == 0 && close(terminal) == 0 && close(noise) == 0);
    CHECK(descriptors() == initial);
    puts("GKD_APP_EXEC_REPORT=PASS cases=18 legacy-API-unchanged=PASS collision-containment=PASS fd-leaks=0");
    return 0;
}
