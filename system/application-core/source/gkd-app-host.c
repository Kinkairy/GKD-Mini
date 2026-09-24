/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-prepare.h"
#include "gkd-app-transfer.h"
#include "gkd-app-release.h"
#include "gkd-app-init.h"
#include "gkd-app-loop.h"
#include "gkd-app-diagnostics.h"
#include "gkd-app-profile.h"
#include "gkd-app-animation.h"
#include "gkd-app-display.h"
#include "gkd-app-controls.h"
#include "gkd-app-service.h"
#include <sys/socket.h>
#include "gkd-update-sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "gkd-app-storage.h"

struct app_file { const char *path, *resolved, *sha256; };
#include "gkd-app-manifest.generated.h"
#ifndef APP_PROFILE
#define APP_PROFILE "/run/gkd-app-profile"
#define APP_INIT "/bin/busybox"
#define APP_LIBRARY "/usr/lib/libgkd-sm-present.so"
#endif
#ifndef APP_GAME_CLIENT
#define APP_GAME_CLIENT "/usr/sbin/gkd-app-game"
#endif
#ifndef APP_FPS_LIBRARY
#define APP_FPS_LIBRARY "/usr/lib/libgkd-fps-present.so"
#endif
static char profile[256] = APP_PROFILE;
static char profile_inittab[288], profile_library[288], profile_fps_library[288];
static int ready_pair[2], error_pair[2], release_pair[2];
static int init_image, controller_pidns, controller_mntns;
static unsigned char launch_token[16];
static char loop_owner[41];
static unsigned timeout_ms;
static pid_t host_pid;
static int host_guard;
static struct gkd_app_diagnostics diagnostics = GKD_APP_DIAGNOSTICS_INIT;
static struct gkd_app_animation animation = GKD_APP_ANIMATION_INIT;
static struct gkd_app_controls controls = GKD_APP_CONTROLS_INIT;
static volatile sig_atomic_t stopping;
static int service_fd = -1;
static void request_stop(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}
static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void step(const char *name)
{
    int saved = errno;
    fprintf(stderr, "GKD_APP_STAGE=%s errno=%d uptime_ms=%llu\n", name, saved,
            (unsigned long long)now_ms());
    fflush(stderr);
}
static void observe_pause(int milliseconds)
{
    gkd_app_diagnostics_pump(&diagnostics);
    (void)poll(NULL, 0, milliseconds);
    gkd_app_diagnostics_pump(&diagnostics);
}
static int directory(const char *p, mode_t mode)
{
    struct stat s;
    if (mkdir(p, mode) && errno != EEXIST) return -1;
    if (lstat(p, &s) || !S_ISDIR(s.st_mode) || s.st_uid != 0) {
        errno = EPERM; return -1;
    }
    return 0;
}
static int verify_fd(int fd, const char *expected)
{
    struct gkdu_sha256 hash;
    struct stat s;
    unsigned char data[32768], digest[32];
    char actual[65];
    if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid != 0 ||
        (s.st_mode & 0022) || lseek(fd, 0, SEEK_SET) != 0) return -1;
    gkdu_sha256_init(&hash);
    for (;;) {
        ssize_t n = read(fd, data, sizeof(data));
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (!n) break;
        gkdu_sha256_update(&hash, data, (size_t)n);
    }
    gkdu_sha256_final(&hash, digest);
    for (unsigned i = 0; i < 32; ++i) sprintf(actual + i * 2, "%02x", digest[i]);
    if (strcmp(actual, expected)) { errno = EBADMSG; return -1; }
    return lseek(fd, 0, SEEK_SET) == 0 ? 0 : -1;
}
static int verified_open(const char *path, const char *hash)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (!verify_fd(fd, hash)) return fd;
    int saved = errno; close(fd); errno = saved; return -1;
}
#ifndef GKD_APP_HOST_NATIVE_TEST
static const char root_path[] = "/newroot";
static int prepare_root(void)
{
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        directory(root_path, 0755) ||
        mount("/dev/mmcblk0p1", root_path, "ext4", MS_RDONLY | MS_NOATIME, "noload"))
        return -1;
    step("P1_RO");
    int asset = verified_open("/newroot" APP_ANIMATION_PATH, APP_ANIMATION_SHA256);
    if (asset < 0) { step("ANIMATION_ASSET_FAILED"); return -1; }
    /* The A kernel build forbids FRAMEBUFFER_CONSOLE; its only console is
     * dummy. Select SM's VT before this sole painter touches scanout. */
    int display = gkd_app_display_prepare(1000);
    if (display) {
        int saved = errno; close(asset); errno = saved;
        step("DISPLAY_PREPARE_FAILED"); return -1;
    }
    int started = gkd_app_animation_begin(&animation, asset, timeout_ms);
    close(asset);
    if (started) { step("ANIMATION_START_FAILED"); return -1; }
    step("ANIMATION_STARTED");
    for (unsigned i = 0; i < sizeof(app_files)/sizeof(app_files[0]); ++i) {
        char p[512], alias[512]; struct stat original, linked;
        snprintf(p, sizeof(p), "%s%s", root_path, app_files[i].resolved);
        snprintf(alias, sizeof(alias), "%s%s", root_path, app_files[i].path);
        int fd = verified_open(p, app_files[i].sha256);
        if (fd < 0) { fprintf(stderr, "GKD_APP_DEPENDENCY=%s\n", app_files[i].path); return -1; }
        if (fstat(fd, &original) || stat(alias, &linked) ||
            original.st_dev != linked.st_dev || original.st_ino != linked.st_ino) {
            close(fd); errno = EBADMSG; return -1;
        }
        close(fd);
    }
    if (gkd_app_animation_stage(&animation, 2)) return -1;
    const char *tmpfs[] = {"/newroot/var", "/newroot/media", "/newroot/mnt"};
    for (unsigned i = 0; i < 3; ++i)
        if (mount("tmpfs", tmpfs[i], "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755,size=16m"))
            return -1;
    const char *dirs[] = {"/newroot/var/run", "/newroot/var/tmp", "/newroot/var/log",
        "/newroot/var/lock", "/newroot/media/data", "/newroot/media/sdcard",
        "/newroot/mnt/SimpleMenu", "/newroot/var/run/gkd-app"};
    for (unsigned i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i)
        if (directory(dirs[i], 0755)) return -1;
    if (mount("/dev", "/newroot/dev", NULL, MS_BIND | MS_REC, NULL) ||
        mount("/sys", "/newroot/sys", NULL, MS_BIND | MS_REC, NULL) ||
        mount("proc", "/newroot/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) ||
        mount("/dev/mmcblk0p2", "/newroot/media/data", "ext3", MS_NOATIME | MS_NODIRATIME, NULL))
        return -1;
    /* The launcher and outer input producer must share one guard/epoch.
     * P1 /run points into its private /var; a second guard would not coordinate
     * key releases with the native menu. Carry the exact existing directory. */
    struct stat input_owner;
    if (lstat("/run/gkd-menu-owner", &input_owner) ||
        !S_ISDIR(input_owner.st_mode) || input_owner.st_uid || input_owner.st_gid ||
        (input_owner.st_mode & 07777) != 0700 ||
        directory("/newroot/var/run/gkd-menu-owner", 0700) ||
        mount("/run/gkd-menu-owner", "/newroot/var/run/gkd-menu-owner",
              NULL, MS_BIND, NULL))
        return -1;
    step("P2_HOME");
    if (gkd_app_animation_stage(&animation, 3)) return -1;
    /* Game card is optional for the first launcher screen; never repair FAT here. */
    if (mount("/dev/mmcblk1p1", "/newroot/media/sdcard", "vfat",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "utf8"))
        step("GAME_CARD_UNAVAILABLE");
    int opk = verified_open("/newroot/media/data/apps/SimpleMenu-OD-v1.1.opk", APP_OPK_SHA256);
    if (opk < 0) return -1;
    int result = gkd_app_loop_mount(opk, "/newroot/mnt/SimpleMenu", loop_owner);
    close(opk);
    if (result) return -1;
    if (gkd_storage_frontend()) return -1;
    if (gkd_app_animation_stage(&animation, 4)) return -1;
    if (gkd_app_bind_readonly(profile_inittab, "/newroot/etc/inittab") ||
        gkd_app_bind_readonly(APP_INIT, "/newroot/bin/busybox") ||
        gkd_app_bind_readonly("/usr/libexec/gkd-app-launcher", "/newroot/usr/bin/opkrun") ||
        gkd_app_bind_readonly("/usr/libexec/gkd-simplemenu-opkrun", "/newroot/mnt/SimpleMenu/opkrun") ||
        gkd_app_profile_mount(profile, "/newroot/var/run/gkd-app"))
        return -1;
    if (gkd_app_update_entry_mount("/run/gkd-application/control.sock",
            "/usr/sbin/gkd-application-service", root_path)) return -1;
    if (gkd_app_animation_stage(&animation, 5) ||
        gkd_app_animation_finish(&animation)) { step("ANIMATION_STOP_FAILED"); return -1; }
    step("ANIMATION_REAPED");
    if (chroot(root_path) || chdir("/mnt/SimpleMenu")) return -1;
    return 0;
}
#else
/* Native fixture supplies isolated synthetic root construction, never card IO. */
#include "gkd-app-host-native.generated.h"
#endif
static int worker(void *unused)
{
    (void)unused;
    if (service_fd >= 0) close(service_fd);
    close(diagnostics.reader); close(diagnostics.file);
    close(ready_pair[0]); close(error_pair[0]); close(release_pair[0]);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != 0) _exit(125);
    struct pollfd guard = {host_guard, POLLIN, 0};
    if (poll(&guard, 1, 0) != 0) _exit(125);
    close(host_guard);
    if (prepare_root()) {
        int saved = errno;
        (void)gkd_app_animation_finish(&animation);
        errno = saved; step("ROOT_FAILED"); _exit(125);
    }
    step("ROOT_READY");
    int app = verified_open("/mnt/SimpleMenu/simplemenu.real", APP_SM_SHA256);
    int terminal = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (app < 0 || terminal < 0) { step("APP_OPEN_FAILED"); _exit(125); }
    char *argv[] = {"/mnt/SimpleMenu/simplemenu.real", NULL};
    char *env[] = {"HOME=/usr/local/home",
        "PATH=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin",
        "TERM=linux", "SDL_VIDEODRIVER=fbcon", "SDL_FBDEV=/dev/fb0",
        "SDL_NOMOUSE=1", "ALSA_CARD=GCW0", NULL};
    struct gkd_app_prepare_request r = {
        .app = {.preparer_pid=1, .executable_fd=app, .terminal_fd=terminal,
            .output_fd=diagnostics.writer,
            .ready_fd=ready_pair[1], .preload_path="/var/run/gkd-app/libgkd-sm-present.so",
            .launch_token=launch_token, .argv=argv, .base_env=env},
        .init_executable_fd=init_image, .error_fd=error_pair[1], .release_fd=release_pair[1],
        .controller_pidns_fd=controller_pidns, .controller_mntns_fd=controller_mntns,
        .controller_mapped_pid=0, .controller_mapped_uid=0, .timeout_ms=timeout_ms};
    (void)gkd_app_prepare_replace(&r);
    step("PREPARE_FAILED");
    _exit(125);
}
static int profile_prepare(void)
{
    if (snprintf(profile_inittab, sizeof(profile_inittab), "%s/inittab", profile) >= (int)sizeof(profile_inittab) ||
        snprintf(profile_library, sizeof(profile_library), "%s/libgkd-sm-present.so", profile) >= (int)sizeof(profile_library) ||
        snprintf(profile_fps_library, sizeof(profile_fps_library), "%s/libgkd-fps-present.so", profile) >= (int)sizeof(profile_fps_library)) {
        errno = ENAMETOOLONG; return -1;
    }
    if (directory(profile, 0700)) return -1;
    int fd = open(profile_inittab, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0400);
    if (fd < 0) return -1;
    close(fd);
    fd = open(profile_library,
              O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0500);
    if (fd < 0) return -1;
    close(fd);
    fd = open(profile_fps_library,
              O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0500);
    if (fd < 0) return -1;
    close(fd);
    char catalog[320];
    snprintf(catalog,sizeof(catalog),"%s/input-routing.conf",profile);
    int menu_config=open(catalog,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0400);
    if(menu_config<0)return -1;
    close(menu_config);
    if(gkd_app_bind_readonly("/etc/gkd-mini/input-routing.conf",catalog))return -1;
    char config_root[320];
    snprintf(config_root,sizeof(config_root),"%s/input-config",profile);
    if(mkdir(config_root,0700)||gkd_app_bind_readonly("/run/gkd-config",config_root))return -1;
    char game_client[320], owner_file[320];
    snprintf(game_client,sizeof(game_client),"%s/gkd-app-game",profile);
    snprintf(owner_file,sizeof(owner_file),"%s/loop-owner",profile);
    int game = open(game_client,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0500);
    if(game<0)return -1;
    close(game);
    int owner = open(owner_file,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0400);
    if(owner<0)return -1;
    close(owner);
    if(gkd_app_bind_readonly(APP_GAME_CLIENT,game_client) ||
       gkd_app_bind_readonly(APP_LIBRARY,profile_library))return -1;
    return gkd_app_bind_readonly(APP_FPS_LIBRARY,profile_fps_library);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        puts("GKD_APP_HOST=self-test protocol=shared-C profile=application-minimal"); return 0;
    }
    if (geteuid() || getpid() == 1 || (argc != 2 && argc != 4)) return 64;
    if (argc == 4) {
        struct ucred peer; socklen_t length = sizeof(peer); int type; socklen_t type_length = sizeof(type);
        if (strcmp(argv[2], "--service-fd") || strcmp(argv[3], "3") ||
            getsockopt(3, SOL_SOCKET, SO_TYPE, &type, &type_length) || type != SOCK_SEQPACKET ||
            getsockopt(3, SOL_SOCKET, SO_PEERCRED, &peer, &length) || peer.uid || peer.pid != getppid()) return 64;
        service_fd = 3;
        if (fcntl(service_fd, F_SETFD, FD_CLOEXEC)) return 1;
    }
    unsigned long parsed = 0;
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || parsed > 120000) return 64;
        parsed = parsed * 10 + (unsigned)(*p - '0');
    }
    if (parsed < 5000 || parsed > 120000) return 64;
    timeout_ms = (unsigned)parsed;
    if (syscall(SYS_close_range, service_fd >= 0 ? 4U : 3U, ~0U, 0U)) return 1;
    struct sigaction stop_action;
    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = request_stop;
    if (sigemptyset(&stop_action.sa_mask) || sigaction(SIGTERM, &stop_action, NULL) ||
        sigaction(SIGINT, &stop_action, NULL)) { step("SIGNAL_SETUP_FAILED"); return 1; }
    host_pid = getpid();
    if (service_fd >= 0) {
        /* A session owns its mount namespace. A clean host reap releases its
         * profile binds, so DEBUG can start a new host in the same boot. */
        if (snprintf(profile, sizeof(profile), APP_PROFILE ".%ld", (long)host_pid) >= (int)sizeof(profile) ||
            unshare(CLONE_NEWNS) || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
            step("SERVICE_NAMESPACE_FAILED"); return 1;
        }
    }
    step("RAM_HOST");
    if (profile_prepare()) { step("PROFILE_FAILED"); return 1; }
    init_image = open(APP_INIT, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    controller_pidns = open("/proc/self/ns/pid", O_RDONLY | O_CLOEXEC);
    controller_mntns = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (init_image < 0 || controller_pidns < 0 || controller_mntns < 0 ||
        gkd_app_ready_channel(ready_pair) || gkd_app_watch_error_channel(error_pair) ||
        gkd_app_release_channel(release_pair)) { step("CHANNEL_FAILED"); return 1; }
    uint64_t entropy_deadline = now_ms() + timeout_ms;
    errno = 0; step("ENTROPY_WAIT");
    for (;;) {
        if (stopping) { errno = ECANCELED; step("APP_CANCELLED"); return 1; }
        if (now_ms() >= entropy_deadline) { errno = ETIMEDOUT; step("ENTROPY_FAILED"); return 1; }
        if (!gkd_app_launch_token(launch_token)) {
            if (now_ms() >= entropy_deadline) {
                memset(launch_token, 0, sizeof(launch_token));
                errno = ETIMEDOUT; step("ENTROPY_FAILED"); return 1;
            }
            break;
        }
        if (errno != EAGAIN) { step("ENTROPY_FAILED"); return 1; }
        observe_pause(20);
    }
    errno = 0; step("ENTROPY_READY");
    /* The kernel-visible ownership label is a one-way, domain-separated
     * digest, not the private token used by the application channels. */
    struct gkdu_sha256 owner_hash;
    unsigned char owner_digest[32];
    gkdu_sha256_init(&owner_hash);
    gkdu_sha256_update(&owner_hash, "gkd-loop-owner-v1", 17);
    gkdu_sha256_update(&owner_hash, launch_token, sizeof(launch_token));
    gkdu_sha256_final(&owner_hash, owner_digest);
    memcpy(loop_owner, "gkd-app-", 8);
    for (unsigned i = 0; i < 16; ++i) sprintf(loop_owner + 8 + i * 2, "%02x", owner_digest[i]);
    char owner_file[320], owner_text[42];
    snprintf(owner_file,sizeof(owner_file),"%s/loop-owner",profile);
    snprintf(owner_text,sizeof(owner_text),"%s\n",loop_owner);
    int owner_fd=open(owner_file,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
    if(owner_fd<0 || write(owner_fd,owner_text,41)!=41 || close(owner_fd)) {
        step("LOOP_OWNER_EXPORT_FAILED");return 1;
    }
    void *stack = malloc(256 * 1024);
    if (!stack) return 1;
    host_guard = (int)syscall(SYS_pidfd_open, host_pid, 0);
    if (host_guard < 0) return 1;
    if (gkd_app_diagnostics_open(&diagnostics, profile)) { step("DIAGNOSTICS_FAILED"); return 1; }
    pid_t init_pid = clone(worker, (char *)stack + 256 * 1024,
                           CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);
    if (init_pid < 0) { step("CLONE_FAILED"); gkd_app_diagnostics_close(&diagnostics); return 1; }
    close(diagnostics.writer); diagnostics.writer = -1;
    close(host_guard);
    close(ready_pair[1]); close(error_pair[1]); close(release_pair[1]);
    close(controller_pidns); close(controller_mntns);
    int init_fd = (int)syscall(SYS_pidfd_open, init_pid, 0);
    struct gkd_app_transfer receipt = GKD_APP_TRANSFER_INIT;
    struct gkd_app_watch watch = GKD_APP_WATCH_INIT;
    struct gkd_app_release_tx release = GKD_APP_RELEASE_TX_INIT;
    int result = 1;
    pid_t reaped = -1;
    if (init_fd < 0 || gkd_app_transfer_init(&receipt, error_pair[0], init_fd,
        init_pid, 0, launch_token, now_ms(), timeout_ms)) goto cleanup;
    while (!stopping && gkd_app_transfer_poll(&receipt, now_ms()) == GKD_TRANSFER_WAITING)
        observe_pause(20);
    if (stopping) { errno = ECANCELED; step("APP_CANCELLED"); goto cleanup; }
    if (receipt.state != GKD_TRANSFER_RECEIVED) { step("TRANSFER_FAILED"); goto cleanup; }
    pid_t app_pid;
    int app_fd = gkd_app_transfer_take(&receipt, &app_pid);
    if (app_fd < 0) goto cleanup;
    if (gkd_app_watch_init(&watch, app_fd, error_pair[0], ready_pair[0],
        app_pid, 0, launch_token, now_ms(), timeout_ms)) { close(app_fd); goto cleanup; }
    step("WAIT_INIT_EXEC");
    /* Release has the same fresh lifecycle window as watch; entropy/root
     * preparation have already consumed their own bounded windows. */
    while (!stopping && gkd_app_release_send(&release, &watch, release_pair[0], init_fd,
                               init_pid, init_image, now_ms())) {
        if (errno != EAGAIN || now_ms() >= watch.ready.deadline_ms) { step("RELEASE_FAILED"); goto cleanup; }
        observe_pause(20);
    }
    if (stopping) { errno = ECANCELED; step("APP_CANCELLED"); goto cleanup; }
    close(release_pair[0]); release_pair[0] = -1;
    errno = 0; step("RELEASE_SENT");
    while (!stopping && gkd_app_watch_poll(&watch, now_ms()) == GKD_WATCH_WAITING)
        observe_pause(20);
    if (stopping) { errno = ECANCELED; step("APP_CANCELLED"); goto cleanup; }
    if (watch.state != GKD_WATCH_FRAME_SUBMITTED) {
        fprintf(stderr, "GKD_APP_FAILED state=%s exec_errno=%d uptime_ms=%llu\n",
            gkd_app_watch_name(watch.state), watch.exec_errno,
            (unsigned long long)now_ms()); goto cleanup;
    }
    if (gkd_app_controls_start(&controls, init_fd, init_pid, now_ms())) {
        step("CONTROLS_START_FAILED"); goto cleanup;
    }
    while (!stopping && gkd_app_watch_poll(&watch, now_ms()) == GKD_WATCH_FRAME_SUBMITTED &&
           gkd_app_controls_poll(&controls, now_ms()) == GKD_APP_CONTROLS_WAITING)
        observe_pause(20);
    if (!stopping && watch.state != GKD_WATCH_FRAME_SUBMITTED) {
        step("APP_EXITED_DURING_CONTROLS_START"); goto cleanup;
    }
    if (stopping || controls.state != GKD_APP_CONTROLS_READY) {
        if (stopping) errno = ECANCELED;
        step(stopping ? "APP_CANCELLED" : "CONTROLS_READY_FAILED"); goto cleanup;
    }
    fprintf(stderr, "GKD_APP_READY pid=%ld host=%ld scope=frame-submitted\n",
            (long)app_pid, (long)host_pid);
    fflush(stderr);
    if (service_fd >= 0) {
        struct gkd_app_service_ready message = {GKD_APP_SERVICE_MAGIC, 1U, host_pid, init_pid, app_pid};
        if (send(service_fd, &message, sizeof(message), MSG_NOSIGNAL) != (ssize_t)sizeof(message)) {
            step("SERVICE_READY_FAILED"); goto cleanup;
        }
    }
    /* No reboot, auto-restart, trial-good or old UI on application exit. */
    while (!stopping && gkd_app_watch_poll(&watch, now_ms()) == GKD_WATCH_FRAME_SUBMITTED) {
        if (gkd_app_controls_poll(&controls, now_ms()) == GKD_APP_CONTROLS_FAILED) {
            step("CONTROLS_EXITED"); goto cleanup;
        }
        observe_pause(100);
    }
    step(stopping ? "APP_CANCELLED" : "APP_EXITED");
cleanup:
    if (service_fd >= 0 && stopping) result = 0;
    gkd_app_diagnostics_pump(&diagnostics);
    /* Bound both the graceful flush and forced reap. A failed clock must not
     * turn cleanup into an infinite wait; each pause has a finite attempt cap. */
    for (unsigned attempt=0; controls.pid > 0 && attempt<110U; ++attempt) {
        enum gkd_app_controls_state state = gkd_app_controls_stop(&controls, now_ms());
        if (state != GKD_APP_CONTROLS_STOPPING) break;
        observe_pause(20);
    }
    if (controls.pid > 0) {
        (void)kill(controls.pid, SIGKILL); /* unreaped direct child; PID cannot be reused */
        for (unsigned attempt=0; attempt<50U; ++attempt) {
            int status;
            pid_t got = waitpid(controls.pid, &status, WNOHANG);
            if (got == controls.pid) {
                controls.pid = -1; controls.exit_status = status; break;
            }
            if (got < 0 && errno != EINTR) break;
            observe_pause(20);
        }
    }
    if (controls.pid > 0) {
        step("CONTROLS_REAP_FAILED"); result = 1;
    } else if (controls.exit_status >= 0) {
        fprintf(stderr, "GKD_APP_CONTROLS_REAPED status=%d clean=%d\n", controls.exit_status,
            WIFEXITED(controls.exit_status) && WEXITSTATUS(controls.exit_status) == 0);
        if (service_fd >= 0 && (!WIFEXITED(controls.exit_status) || WEXITSTATUS(controls.exit_status)))
            result = 1;
    }
    /* close refuses to discard a live child's identity; hold teardown below. */
    gkd_app_controls_close(&controls);
    /* A successful reap is required before clearing loop mappings: the old
     * driver refuses detach while the application mount still owns a handle. */
    if (controls.pid > 0) {
        step("NAMESPACE_HELD_BY_CONTROLS");
        result = 1;
        reaped = -1;
    } else if (init_fd >= 0) {
        (void)syscall(SYS_pidfd_send_signal, init_fd, SIGKILL, NULL, 0);
        do { reaped = waitpid(init_pid, NULL, 0); } while (reaped < 0 && errno == EINTR);
        close(init_fd);
    } else {
        (void)kill(init_pid, SIGKILL);
        do { reaped = waitpid(init_pid, NULL, 0); } while (reaped < 0 && errno == EINTR);
    }
    if (reaped != init_pid) { step("NAMESPACE_REAP_FAILED"); result = 1; }
    else if (gkd_app_loop_cleanup(loop_owner)) { step("LOOP_CLEANUP_FAILED"); result = 1; }
    gkd_app_diagnostics_finish(&diagnostics);
    fprintf(stderr, "GKD_APP_DIAGNOSTICS retained=%llu discarded=%llu capture_errno=%d read_errno=%d eof=%d\n",
            (unsigned long long)diagnostics.retained,
            (unsigned long long)diagnostics.discarded,
            diagnostics.capture_errno, diagnostics.read_errno, diagnostics.eof);
    gkd_app_diagnostics_close(&diagnostics);
    gkd_app_transfer_close(&receipt);
    gkd_app_watch_close(&watch);
    free(stack);
    return result;
}
