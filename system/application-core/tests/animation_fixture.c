/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-animation.h"
#include "round38-animation-format.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME GKD_ROUND38_FRAME_BYTES
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d errno %d: %s\n", __LINE__, errno, #x); exit(1); } } while (0)
struct shared {
    volatile unsigned pans, page, leaked;
    int open_error, ioctl_error, geometry_bad, stall, fake_wait, clock_error;
    unsigned virtual_height, initial_offset;
    pid_t owner;
};
static struct shared *state;
static int framebuffer;
int __real_open(const char *, int, ...);
int __real_clock_gettime(clockid_t, struct timespec *);
pid_t __real_waitpid(pid_t, int *, int);

int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{
    if (state && state->clock_error && getpid() == state->owner) {
        errno = EIO; return -1;
    }
    return __real_clock_gettime(clock, ts);
}
pid_t __wrap_waitpid(pid_t pid, int *status, int options)
{
    if (state && state->fake_wait && getpid() == state->owner && pid > 0)
        return 0;
    return __real_waitpid(pid, status, options);
}
int __wrap_open(const char *path, int flags, ...)
{
    if (!strcmp(path, "/dev/fb0")) {
        if (fcntl(199, F_GETFD) >= 0) state->leaked = 1;
        if (state->open_error) { errno = ENODEV; return -1; }
        return __real_open("/test/framebuffer", flags, 0600);
    }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags);
        mode = (mode_t)va_arg(args, int); va_end(args);
    }
    return __real_open(path, flags, mode);
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args; va_start(args, request);
    void *value = va_arg(args, void *); va_end(args);
    if (fd < 0 || state->ioctl_error == (request == FBIOGET_VSCREENINFO ? 1 :
        request == FBIOGET_FSCREENINFO ? 2 : 3)) { errno = EIO; return -1; }
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v = value; memset(v, 0, sizeof(*v));
        v->xres = 320; v->yres = 240; v->xres_virtual = state->geometry_bad ? 321 : 320;
        v->yres_virtual = state->virtual_height ? state->virtual_height : 480;
        v->yoffset = state->initial_offset; v->bits_per_pixel = 16;
        v->red.offset = 11; v->red.length = 5;
        v->green.offset = 5; v->green.length = 6; v->blue.length = 5;
        return 0;
    }
    if (request == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *v = value; memset(v, 0, sizeof(*v));
        v->line_length = 640; v->smem_len = FRAME * 2; return 0;
    }
    if (request == FBIOPAN_DISPLAY) {
        struct fb_var_screeninfo *v = value;
        CHECK(v->xoffset == 0 && (v->yoffset == 0 || v->yoffset == 240));
        if (state->stall && state->pans) raise(SIGSTOP);
        state->page = v->yoffset / 240;
        __sync_synchronize(); ++state->pans; return 0;
    }
    errno = EINVAL; return -1;
}
static uint64_t now(void)
{
    struct timespec t; CHECK(__real_clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int asset(int malformed, int truncated)
{
    unsigned char header[32] = {0};
    int fd = memfd_create("animation-test-asset", MFD_CLOEXEC); CHECK(fd >= 0);
    memcpy(header, "GKDANIM1", 8);
    header[8] = 64; header[9] = 1; header[10] = 240; header[12] = 31; header[14] = 100;
    memcpy(header + 16, "RGB565LE", 8);
    for (unsigned i = 0; i < 4; ++i) header[24 + i] = GKD_ROUND38_PAYLOAD_BYTES >> (8 * i);
    if (malformed) header[0] = 'X';
    CHECK(write(fd, header, sizeof(header)) == sizeof(header));
    unsigned short *frame = malloc(FRAME); CHECK(frame);
    for (unsigned n = 0; n < 31 && !truncated; ++n) {
        for (unsigned i = 0; i < FRAME / 2; ++i) frame[i] = 0x100 + n;
        CHECK(write(fd, frame, FRAME) == FRAME);
    }
    free(frame); CHECK(lseek(fd, 0, SEEK_SET) == 0); return fd;
}
static void setup(void)
{
    memset(state, 0, sizeof(*state)); state->owner = getpid();
    framebuffer = __real_open("/test/framebuffer", O_RDWR | O_CREAT | O_TRUNC, 0600);
    CHECK(framebuffer >= 0 && ftruncate(framebuffer, FRAME * 2) == 0);
    void *pixels = mmap(NULL, FRAME * 2, PROT_READ | PROT_WRITE, MAP_SHARED, framebuffer, 0);
    CHECK(pixels != MAP_FAILED); memset(pixels, 0xa5, FRAME * 2);
    CHECK(munmap(pixels, FRAME * 2) == 0);
}
static unsigned short pixel(unsigned page, unsigned x, unsigned y)
{
    unsigned short value;
    CHECK(pread(framebuffer, &value, 2, page * FRAME + (y * 320 + x) * 2) == 2);
    return value;
}
static void no_child(void)
{
    int status; errno = 0;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}
static void close_case(int fd, const char *name)
{
    CHECK(close(fd) == 0 && close(framebuffer) == 0);
    no_child(); printf("CASE=%s PASS\n", name);
}
static void progress(void)
{
    unsigned short *original = malloc(FRAME), *frame = malloc(FRAME);
    CHECK(original && frame); memset(original, 0x6c, FRAME);
    unsigned previous = 0;
    for (unsigned stage = 1; stage <= 5; ++stage) {
        memcpy(frame, original, FRAME); gkd_app_animation_progress(frame, stage);
        unsigned filled = 0;
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x) {
                if (x < 121 || x > 198 || y < 193 || y > 198)
                    CHECK(frame[y * 320 + x] == original[y * 320 + x]);
                else if (frame[y * 320 + x] == 0x8eaa || frame[y * 320 + x] == 0x7e28)
                    ++filled;
            }
        CHECK(filled > previous && filled < 78 * 6); previous = filled;
    }
    memcpy(frame, original, FRAME); gkd_app_animation_progress(frame, 6);
    CHECK(!memcmp(frame, original, FRAME)); free(frame); free(original);
    puts("CASE=progress-outside-preserved-no-false-complete PASS");
}
int main(void)
{
    state = mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(state != MAP_FAILED); progress();
    struct gkd_app_animation a = GKD_APP_ANIMATION_INIT;
    setup(); int fd = asset(0, 0);
    CHECK(dup2(fd, 199) == 199); /* A deliberately leaked caller descriptor. */
    CHECK(gkd_app_animation_begin(&a, fd, 2000) == 0);
    CHECK(a.pid > 0 && a.stage == 1 && state->pans && !state->leaked);
    unsigned short first = pixel(state->page, 0, 0);
    CHECK(first >= 0x100 && first <= 0x11e);
    uint64_t until = now() + 600;
    while (pixel(state->page, 0, 0) == first && now() < until) usleep(5000);
    CHECK(pixel(state->page, 0, 0) != first);
    CHECK(gkd_app_animation_stage(&a, 3) == 0);
    CHECK(gkd_app_animation_stage(&a, 2) == -1 && gkd_app_animation_stage(&a, 6) == -1);
    CHECK(gkd_app_animation_stage(&a, 5) == 0);
    CHECK(gkd_app_animation_finish(&a) == 0);
    CHECK(a.pid == -1 && a.command == -1 && a.stage == 0);
    CHECK(pixel(state->page, 121, 193) == 0x7e28);
    CHECK(pixel(state->page, 198, 193) == 0x1922);
    unsigned char *before = malloc(FRAME * 2), *after = malloc(FRAME * 2);
    CHECK(before && after && pread(framebuffer, before, FRAME * 2, 0) == FRAME * 2);
    unsigned pans = state->pans; usleep(180000);
    CHECK(pread(framebuffer, after, FRAME * 2, 0) == FRAME * 2);
    CHECK(!memcmp(before, after, FRAME * 2) && state->pans == pans);
    free(before); free(after); CHECK(close(199) == 0);
    close_case(fd, "motion-stage-stop-reap-no-posthandoff-writes-fd-containment");

    setup(); fd = asset(0, 0);
    state->virtual_height = 720; state->initial_offset = 480;
    CHECK(gkd_app_animation_begin(&a, fd, 1000) == 0);
    CHECK(state->pans && state->page <= 1);
    CHECK(gkd_app_animation_finish(&a) == 0);
    close_case(fd, "legacy-third-page-handoff-with-two-page-mapping");
    for (unsigned bad = 0; bad < 3; ++bad) {
        setup(); fd = asset(0, 0);
        state->virtual_height = bad == 0 ? 960 : 720;
        state->initial_offset = bad == 1 ? 720 : bad == 2 ? 1 : 0;
        CHECK(gkd_app_animation_begin(&a, fd, 1000) == -1);
        close_case(fd, "invalid-virtual-height-or-offset");
    }

    for (unsigned fault = 0; fault < 6; ++fault) {
        setup(); fd = asset(fault == 0, fault == 1);
        state->open_error = fault == 2;
        state->ioctl_error = fault >= 3 ? (int)fault - 2 : 0;
        CHECK(gkd_app_animation_begin(&a, fd, 1000) == -1 && a.pid == -1);
        close_case(fd, "malformed-truncated-open-ioctl-refusal");
    }
    setup(); fd = asset(0, 0); state->geometry_bad = 1;
    CHECK(gkd_app_animation_begin(&a, fd, 1000) == -1 && a.pid == -1);
    close_case(fd, "geometry-refusal");

    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 150) == 0); usleep(220000);
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid == -1);
    close_case(fd, "deadline-exit");
    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 1000) == 0);
    CHECK(shutdown(a.command, SHUT_WR) == 0);
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid == -1);
    close_case(fd, "control-eof");
    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 1000) == 0);
    CHECK(kill(a.pid, SIGKILL) == 0);
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid == -1);
    close_case(fd, "early-painter-death");
    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 2000) == 0);
    CHECK(ftruncate(fd, 32) == 0); usleep(160000);
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid == -1);
    close_case(fd, "mid-animation-read-failure");
    setup(); fd = asset(0, 0); state->stall = 1;
    CHECK(gkd_app_animation_begin(&a, fd, 2000) == 0); usleep(160000);
    uint64_t start = now();
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid == -1);
    CHECK(now() - start < 1200);
    close_case(fd, "stalled-painter-forced-reap");
    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 2000) == 0);
    state->fake_wait = 1; start = now();
    CHECK(gkd_app_animation_finish(&a) == -1 && a.pid > 0);
    CHECK(now() - start < 1400); state->fake_wait = 0;
    int status; CHECK(waitpid(a.pid, &status, 0) == a.pid);
    close(a.command); a = (struct gkd_app_animation)GKD_APP_ANIMATION_INIT;
    close_case(fd, "unreapable-refuses-handoff-within-bound");
    setup(); fd = asset(0, 0);
    CHECK(gkd_app_animation_begin(&a, fd, 2000) == 0);
    state->clock_error = 1; start = now();
    CHECK(gkd_app_animation_finish(&a) == -1);
    CHECK(now() - start < 200); state->clock_error = 0;
    if (a.pid > 0) (void)gkd_app_animation_finish(&a);
    close_case(fd, "clock-failure-bounded");
    setup(); fd = asset(0, 0);
    CHECK(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    int notice[2]; CHECK(pipe2(notice, O_CLOEXEC) == 0);
    pid_t owner = fork(); CHECK(owner >= 0);
    if (!owner) {
        close(notice[0]);
        struct gkd_app_animation orphan = GKD_APP_ANIMATION_INIT;
        if (gkd_app_animation_begin(&orphan, fd, 2000)) _exit(2);
        if (write(notice[1], &orphan.pid, sizeof(orphan.pid)) != sizeof(orphan.pid)) _exit(3);
        _exit(0); /* Deliberately omit finish: exercise real PDEATHSIG. */
    }
    close(notice[1]); pid_t painter;
    CHECK(read(notice[0], &painter, sizeof(painter)) == sizeof(painter));
    close(notice[0]);
    CHECK(waitpid(owner, &status, 0) == owner && WIFEXITED(status) && !WEXITSTATUS(status));
    start = now();
    pid_t reaped;
    do { reaped = waitpid(painter, &status, WNOHANG); if (!reaped) usleep(5000); }
    while (!reaped && now() - start < 600);
    CHECK(reaped == painter && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    pans = state->pans; usleep(150000); CHECK(state->pans == pans);
    CHECK(prctl(PR_SET_CHILD_SUBREAPER, 0) == 0);
    close_case(fd, "parent-death-kills-painter-no-later-writes");
    CHECK(munmap(state, sizeof(*state)) == 0);
    puts("GKD_APP_ANIMATION=PASS cases=21 sanitizer=ASan+UBSan");
    return 0;
}
