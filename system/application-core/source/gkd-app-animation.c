/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-animation.h"
#include "round38-animation-format.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_BYTES GKD_ROUND38_FRAME_BYTES
#define MAP_BYTES (FRAME_BYTES * 2)
static uint64_t milliseconds(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static long asset_read(void *context, unsigned char *buffer, unsigned long size)
{
    ssize_t n;
    do { n = read((int)(long)context, buffer, size); } while (n < 0 && errno == EINTR);
    return n;
}
static int exact(int fd, void *buffer, unsigned long size)
{
    return gkd_round38_read_exact(asset_read, (void *)(long)fd, buffer, size);
}
/* Keep the accepted rail and every pixel outside its baked interior unchanged.
 * Five completed preparation stages out of six; menu readiness owns the sixth
 * and is never painted by this process after display ownership is transferred. */
void gkd_app_animation_progress(unsigned short *pixels, unsigned stage)
{
    if (!pixels || stage < 1 || stage > 5) return;
    unsigned filled = 78 * stage / 6;
    for (unsigned y = 193; y <= 198; ++y)
        for (unsigned x = 121; x <= 198; ++x)
            pixels[y * 320 + x] = x - 121 < filled ?
                ((x + y) & 1 ? 0x8eaa : 0x7e28) :
                ((x + y) & 1 ? 0x1922 : 0x2143);
}
static int painter(int socket_fd, int asset_fd, unsigned timeout)
{
    struct fb_var_screeninfo pan;
    struct fb_fix_screeninfo fixed;
    unsigned short *mapping = MAP_FAILED, *frame = NULL;
    int fb = -1, result = -1, ready_sent = 0;
    unsigned stage = 1, index = 0, page;
    uint64_t started = milliseconds(), deadline = started + timeout;
    uint64_t next = started;
    fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb < 0) goto done;
    memset(&pan, 0, sizeof(pan)); memset(&fixed, 0, sizeof(fixed));
    if (ioctl(fb, FBIOGET_VSCREENINFO, &pan) ||
        ioctl(fb, FBIOGET_FSCREENINFO, &fixed) ||
        pan.xres != 320 || pan.yres != 240 || pan.bits_per_pixel != 16 ||
        pan.xres_virtual != 320 || (pan.yres_virtual != 480 && pan.yres_virtual != 720) ||
        pan.yoffset % 240 || pan.yoffset > pan.yres_virtual - 240 ||
        fixed.line_length != 640 || fixed.smem_len < MAP_BYTES ||
        pan.red.offset != 11 || pan.red.length != 5 ||
        pan.green.offset != 5 || pan.green.length != 6 ||
        pan.blue.offset != 0 || pan.blue.length != 5 ||
        pan.red.msb_right || pan.green.msb_right || pan.blue.msb_right || pan.transp.length) { errno = EINVAL; goto done; }
    mapping = mmap(NULL, MAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (mapping == MAP_FAILED) goto done;
    frame = malloc(FRAME_BYTES);
    if (!frame) goto done;
    /* A legacy game may leave page 2 active; use our mapped pages 0/1. */
    page = pan.yoffset == 240;
    for (;;) {
        uint64_t now = milliseconds();
        if (!now || now >= deadline) { errno = ETIMEDOUT; goto done; }
        struct pollfd pollfd = {socket_fd, POLLIN, 0};
        int wait = next > now ? (int)(next - now) : 0;
        if ((uint64_t)wait > deadline - now) wait = (int)(deadline - now);
        int polled;
        do { polled = poll(&pollfd, 1, wait); } while (polled < 0 && errno == EINTR);
        if (polled < 0) goto done;
        int changed = 0;
        if (polled) {
            unsigned char message[2];
            ssize_t n = recv(socket_fd, message, sizeof(message), 0);
            if (n != 1) { errno = EPIPE; goto done; }
            if (message[0] == 0) { result = 0; goto done; }
            if (message[0] < stage || message[0] > 5) { errno = EPROTO; goto done; }
            stage = message[0];
            changed = 1;
        }
        now = milliseconds();
        if (!now || now >= deadline) { errno = ETIMEDOUT; goto done; }
        if (now < next && !changed) continue;
        if (now >= next) {
            uint64_t tick = (now - started) / GKD_ROUND38_FRAME_MILLISECONDS;
            index = tick % GKD_ROUND38_FRAME_COUNT;
            next = started + (tick + 1) * GKD_ROUND38_FRAME_MILLISECONDS;
        }
        off_t offset = GKD_ROUND38_HEADER_BYTES + (off_t)index * FRAME_BYTES;
        if (lseek(asset_fd, offset, SEEK_SET) != offset || exact(asset_fd, frame, FRAME_BYTES)) goto done;
        gkd_app_animation_progress(frame, stage);
        page ^= 1;
        memcpy(mapping + page * 320 * 240, frame, FRAME_BYTES);
        __sync_synchronize();
        pan.xoffset = 0; pan.yoffset = page * 240; pan.activate = FB_ACTIVATE_VBL;
        if (ioctl(fb, FBIOPAN_DISPLAY, &pan)) goto done;
        if (!ready_sent) {
            /* Ready is sent exactly once, independently of stage-triggered draws. */
            unsigned char ready = 'R';
            if (send(socket_fd, &ready, 1, MSG_NOSIGNAL) != 1) goto done;
            ready_sent = 1;
        }
    }
done:
    free(frame);
    if (mapping != MAP_FAILED) munmap(mapping, MAP_BYTES);
    if (fb >= 0) close(fb);
    return result;
}
static int reap(struct gkd_app_animation *a, int requested)
{
    int status = 0, forced = 0;
    uint64_t now = milliseconds(), deadline = now + 500;
    for (;;) {
        pid_t n = waitpid(a->pid, &status, WNOHANG);
        if (n == a->pid) {
            int success = requested && !forced && WIFEXITED(status) && !WEXITSTATUS(status);
            if (a->command >= 0) close(a->command);
            a->pid = -1; a->command = -1; a->stage = 0;
            if (!success) errno = forced ? ETIMEDOUT : EIO;
            return success ? 0 : -1;
        }
        if (n < 0 && errno != EINTR) return -1;
        now = milliseconds();
        if (!now) {
            (void)kill(a->pid, SIGKILL);
            errno = EIO; return -1;
        }
        if (now >= deadline) {
            if (forced) { errno = ETIMEDOUT; return -1; }
            (void)kill(a->pid, SIGKILL);
            forced = 1; deadline = now + 500;
        }
        (void)poll(NULL, 0, 5);
    }
}

int gkd_app_animation_begin(struct gkd_app_animation *a, int asset, unsigned timeout)
{
    struct stat s;
    unsigned char header[GKD_ROUND38_HEADER_BYTES];
    int pair[2];
    if (!a || a->pid != -1 || a->command != -1 || asset < 0 ||
        timeout < 100 || timeout > 120000) { errno = EINVAL; return -1; }
    if (fstat(asset, &s) || !S_ISREG(s.st_mode) ||
        lseek(asset, 0, SEEK_SET) != 0 || exact(asset, header, sizeof(header)) ||
        !gkd_round38_header_valid(header, s.st_size)) { errno = EBADMSG; return -1; }
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair)) return -1;
    pid_t parent = getpid(), child = fork();
    if (child < 0) { close(pair[0]); close(pair[1]); return -1; }
    if (!child) {
        close(pair[0]);
        /* Safe even when source descriptors overlap final descriptor numbers. */
        int control = fcntl(pair[1], F_DUPFD_CLOEXEC, 10);
        int image = fcntl(asset, F_DUPFD_CLOEXEC, 10);
        if (control < 0 || image < 0 || dup3(control, 3, O_CLOEXEC) < 0 ||
            dup3(image, 4, O_CLOEXEC) < 0 ||
            syscall(SYS_close_range, 5U, ~0U, 0U)) _exit(1);
        close(0); close(1);
        struct sigaction action;
        memset(&action, 0, sizeof(action)); action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL) ||
            sigaction(SIGCHLD, &action, NULL) ||
            sigprocmask(SIG_SETMASK, &action.sa_mask, NULL) ||
            prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(1);
        int result = painter(3, 4, timeout);
        close(3); close(4); _exit(result ? 1 : 0);
    }
    close(pair[1]);
    a->pid = child; a->command = pair[0]; a->stage = 1;
    struct pollfd ready = {pair[0], POLLIN, 0};
    int n;
    uint64_t start = milliseconds(), limit = start + 500;
    do {
        uint64_t now = milliseconds();
        if (!start || !now || now >= limit) { n = 0; break; }
        n = poll(&ready, 1, (int)(limit - now));
    } while (n < 0 && errno == EINTR);
    unsigned char message[2];
    if (n <= 0 || recv(pair[0], message, sizeof(message), 0) != 1 || message[0] != 'R') {
        int saved = n == 0 ? ETIMEDOUT : EIO;
        (void)reap(a, 0); errno = saved; return -1;
    }
    return 0;
}
int gkd_app_animation_stage(struct gkd_app_animation *a, unsigned stage)
{
    if (!a || a->pid <= 0 || a->command < 0 || stage < a->stage || stage > 5) {
        errno = EINVAL; return -1;
    }
    unsigned char message = stage;
    if (send(a->command, &message, 1, MSG_NOSIGNAL | MSG_DONTWAIT) != 1) return -1;
    a->stage = stage; return 0;
}
int gkd_app_animation_finish(struct gkd_app_animation *a)
{
    if (!a) { errno = EINVAL; return -1; }
    if (a->pid == -1 && a->command == -1) return 0;
    if (a->pid <= 0 || a->command < 0) { errno = EINVAL; return -1; }
    unsigned char stop = 0;
    int sent = send(a->command, &stop, 1, MSG_NOSIGNAL | MSG_DONTWAIT) == 1;
    return reap(a, sent);
}
