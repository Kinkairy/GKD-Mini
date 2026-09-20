/* SPDX-License-Identifier: GPL-2.0 */
/* Native mount-namespace fixture; it calls the production profile helper. */
#define _GNU_SOURCE
#include "gkd-app-profile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef REFUSE_FILE_BIND
/* Faults are enabled after setup; each production bind/remount is tested. */
int __real_mount(const char *, const char *, const char *, unsigned long, const void *);
static unsigned fault_calls, fault_position;
static int fault_active;
static void activate_fault(unsigned position) { fault_calls = 0; fault_position = position; fault_active = 1; }
int __wrap_mount(const char *source, const char *target, const char *type,
                 unsigned long flags, const void *data)
{
    if (fault_active && ++fault_calls == fault_position) { errno = EPERM; return -1; }
    return __real_mount(source, target, type, flags, data);
}
#endif

static int write_empty(const char *path, mode_t mode)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
    if (fd < 0) return -1;
    return close(fd);
}
static int joined(char *destination, size_t size, const char *base, const char *leaf)
{
    size_t base_size = strlen(base), leaf_size = strlen(leaf);
    if (base_size > size || leaf_size >= size - base_size) { errno = ENAMETOOLONG; return -1; }
    memcpy(destination, base, base_size);
    memcpy(destination + base_size, leaf, leaf_size + 1);
    return 0;
}
static int copy_file(const char *source, const char *target)
{
    char data[8192];
    int in = open(source, O_RDONLY | O_CLOEXEC);
    int out = open(target, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0755);
    if (in < 0 || out < 0) return -1;
    for (;;) {
        ssize_t got = read(in, data, sizeof(data));
        if (got <= 0) { close(in); return close(out) || got < 0 ? -1 : 0; }
        for (ssize_t off = 0; off < got;) {
            ssize_t put = write(out, data + off, (size_t)(got - off));
            if (put <= 0) { close(in); close(out); return -1; }
            off += put;
        }
    }
}
static int expect_readonly(const char *path)
{
    struct statvfs filesystem;
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { close(fd); errno = EACCES; return -1; }
    return errno == EROFS && !statvfs(path, &filesystem) && (filesystem.f_flag & ST_RDONLY) ? 0 : -1;
}
static int expect_directory_readonly(const char *directory)
{
    char path[8192]; struct statvfs filesystem;
    if (joined(path, sizeof(path), directory, "/write-refused") ||
        write_empty(path, 0600) == 0 || errno != EROFS ||
        statvfs(directory, &filesystem) || !(filesystem.f_flag & ST_RDONLY)) return -1;
    return 0;
}
static int same_file(const char *left, const char *right)
{
    char a[8192], b[8192];
    int one = open(left, O_RDONLY | O_CLOEXEC), two = open(right, O_RDONLY | O_CLOEXEC);
    if (one < 0 || two < 0) return -1;
    for (;;) {
        ssize_t first = read(one, a, sizeof(a)), second = read(two, b, sizeof(b));
        if (first != second || first < 0 || (first && memcmp(a, b, (size_t)first))) {
            close(one); close(two); return -1;
        }
        if (!first) { close(one); return close(two); }
    }
}
static int setup(const char *work, const char *library, const char *program,
                 char *profile, char *target, char *root)
{
    char path[8192];
    snprintf(profile, 8192, "%s/profile", work);
    snprintf(root, 8192, "%s/root", work);
    if (mkdir(profile, 0700) || mkdir(root, 0755) ||
        joined(path, sizeof(path), root, "/lib64") || mkdir(path, 0755) ||
        joined(path, sizeof(path), root, "/lib64/ld-linux-x86-64.so.2") ||
        copy_file("/lib64/ld-linux-x86-64.so.2", path) ||
        joined(path, sizeof(path), root, "/lib64/libc.so.6") || copy_file("/lib64/libc.so.6", path) ||
        joined(path, sizeof(path), root, "/var") || mkdir(path, 0755) ||
        joined(path, sizeof(path), root, "/var/run") || mkdir(path, 0755) ||
        joined(path, sizeof(path), root, "/var/run/gkd-app") || mkdir(path, 0755) ||
        joined(target, 8192, root, "/var/run/gkd-app")) return -1;
    if (joined(path, sizeof(path), profile, "/libgkd-sm-present.so") ||
        write_empty(path, 0500) || gkd_app_bind_readonly(library, path) ||
        joined(path, sizeof(path), profile, "/libgkd-fps-present.so") ||
        write_empty(path, 0500) || gkd_app_bind_readonly(library, path) ||
        joined(path, sizeof(path), profile, "/gkd-app-game") ||
        write_empty(path, 0500) ||
        joined(path, sizeof(path), profile, "/input-routing.conf") ||
        write_empty(path, 0400) || gkd_app_bind_readonly(library, path) ||
        joined(path, sizeof(path), profile, "/input-config") || mkdir(path,0700) ||
        joined(path, sizeof(path), profile, "/input-config/effective.conf") || copy_file(library,path) ||
        joined(path, sizeof(path), profile, "/unrelated-nested.so") ||
        write_empty(path, 0500) || gkd_app_bind_readonly(library, path) ||
        joined(path, sizeof(path), target, "/libgkd-sm-present.so") || write_empty(path, 0500) ||
        joined(path, sizeof(path), root, "/app") || copy_file(program, path)) return -1;
    return 0;
}
static int run_child(const char *target)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        if (chroot(target) || chdir("/")) _exit(125);
        setenv("LD_PRELOAD", "/var/run/gkd-app/libgkd-sm-present.so", 1);
        setenv("GKD_PROFILE_MARKER", "/preload-marker", 1);
        execl("/app", "/app", (char *)NULL);
        perror("profile exec");
        _exit(126);
    }
    int status;
    if (waitpid(child, &status, 0) != child) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
int main(int argc, char **argv)
{
    char profile[8192], target[8192], root[8192], path[8192];
    struct stat status;
    if (geteuid() || argc < 3) return 64;
    if (!strcmp(argv[1], "--negative")) {
        if (argc != 3 || joined(profile, sizeof(profile), argv[2], "/profile") ||
            joined(target, sizeof(target), argv[2], "/target") || mkdir(profile, 0700) ||
            mkdir(target, 0755) || joined(path, sizeof(path), target, "/libgkd-sm-present.so") ||
            write_empty(path, 0500) || gkd_app_profile_mount("/does-not-exist", target) == 0 ||
            gkd_app_profile_mount(profile, "/does-not-exist-target") == 0 ||
            gkd_app_profile_mount(profile, target) == 0) return 1;
        puts("GKD_PROFILE_NEGATIVE=PASS");
        return 0;
    }
    if (argc != 5 || (strcmp(argv[1], "--run") && strcmp(argv[1], "--legacy") &&
                      strncmp(argv[1], "--refuse-", 9))) return 64;
    if (setup(argv[2], argv[3], argv[4], profile, target, root)) { perror("profile setup"); return 1; }
    if (!strcmp(argv[1], "--legacy")) {
        /* Historical directory-only bind: intentionally lacks the child file bind. */
        if (gkd_app_bind_readonly(profile, target) ||
            joined(path, sizeof(path), target, "/libgkd-sm-present.so") ||
            stat(path, &status) || status.st_size != 0 ||
            run_child(root) || joined(path, sizeof(path), root, "/preload-marker") ||
            access(path, F_OK) == 0) return 1;
        puts("GKD_PROFILE_LEGACY_DEFECT=REPRODUCED");
        return 0;
    }
    if (!strncmp(argv[1], "--refuse-", 9)) {
#ifdef REFUSE_FILE_BIND
        unsigned position=0;const char *digit=argv[1]+9;
        for(;*digit;digit++){if(*digit<'0'||*digit>'9'||position>12)return 64;position=position*10U+(unsigned)(*digit-'0');}
        if (position < 1 || position > 12) return 64;
        activate_fault(position);
        if (gkd_app_profile_mount(profile, target) == 0 || errno != EPERM ||
            fault_calls != position) return 1;
        if (joined(path, sizeof(path), root, "/preload-marker") || access(path, F_OK) == 0) return 1;
        printf("GKD_PROFILE_REFUSAL=PASS position=%u\n", position);
        return 0;
#else
        return 64;
#endif
    }
    if (gkd_app_profile_mount(profile, target)) { perror("profile mount"); return 1; }
    if (expect_directory_readonly(target) || joined(path, sizeof(path), target, "/libgkd-sm-present.so") ||
        same_file(argv[3], path) || expect_readonly(path) ||
        joined(path, sizeof(path), target, "/libgkd-fps-present.so") ||
        same_file(argv[3], path) || expect_readonly(path) ||
        joined(path, sizeof(path), target, "/input-routing.conf") ||
        same_file(argv[3],path) || expect_readonly(path) ||
        joined(path, sizeof(path), target, "/input-config") || expect_directory_readonly(path) ||
        joined(path, sizeof(path), target, "/input-config/effective.conf") || same_file(argv[3],path) || expect_readonly(path) ||
        joined(path, sizeof(path), target, "/unrelated-nested.so") ||
        stat(path, &status) || status.st_size != 0 ||
        joined(path, sizeof(path), target, "/new-file") ||
        write_empty(path, 0600) == 0 || errno != EROFS) { perror("profile readonly"); return 1; }
    if (run_child(root)) { fputs("profile child failed\n", stderr); return 1; }
    if (joined(path, sizeof(path), root, "/preload-marker") || access(path, R_OK)) return 1;
    puts("GKD_PROFILE_MOUNT=PASS");
    return 0;
}
