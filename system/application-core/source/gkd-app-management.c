/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef GKD_MANAGEMENT_RUN_ROOT
#define GKD_MANAGEMENT_RUN_ROOT "/run/gkd-round64-ssh-hook"
#endif
#ifndef GKD_MANAGEMENT_PROC_ROOT
#define GKD_MANAGEMENT_PROC_ROOT "/proc"
#endif
#ifndef GKD_MANAGEMENT_DROPBEAR
#define GKD_MANAGEMENT_DROPBEAR "/usr/sbin/dropbear"
#endif
#ifndef GKD_MANAGEMENT_HOST_KEY
#define GKD_MANAGEMENT_HOST_KEY "/run/gkd-round64-ssh/dropbear_rsa_host_key"
#endif
#ifndef GKD_MANAGEMENT_INTERFACE
#define GKD_MANAGEMENT_INTERFACE "usb0"
#endif
#ifndef GKD_MANAGEMENT_POLL
#define GKD_MANAGEMENT_POLL poll
#endif

#define GKD_MANAGEMENT_PID_RECORD GKD_MANAGEMENT_RUN_ROOT "/dropbear.pid"
#define GKD_MANAGEMENT_NATIVE_PID GKD_MANAGEMENT_RUN_ROOT "/dropbear.native.pid"
#define GKD_MANAGEMENT_ADDRESS "10.1.1.2"
#define GKD_MANAGEMENT_TCP_ADDRESS "0201010A:0016"
#define GKD_MANAGEMENT_MAX_FILE 2048U
#define GKD_MANAGEMENT_MAX_TCP_LINES 65536U
#define GKD_MANAGEMENT_MAX_FDS 4096U

struct management_identity {
    pid_t pid;
    unsigned long long starttime;
};

static int fail(const char *stage, int error)
{
    if (!error) error = EPROTO;
    fprintf(stderr, "GKD_APP_MANAGEMENT=BLOCKED stage=%s errno=%d\n", stage, error);
    errno = error;
    return 1;
}

static int exact_unsigned(const char *text, unsigned long long maximum,
                          unsigned long long *value)
{
    unsigned long long result = 0;
    if (!text || !*text || !value) { errno = EINVAL; return -1; }
    for (; *text; ++text) {
        unsigned digit;
        if (*text < '0' || *text > '9') { errno = EPROTO; return -1; }
        digit = (unsigned)(*text - '0');
        if (result > (maximum - digit) / 10ULL) { errno = ERANGE; return -1; }
        result = result * 10ULL + digit;
    }
    *value = result;
    return 0;
}

static int read_owned_file(const char *path, unsigned char *data, size_t capacity,
                           size_t *length)
{
    struct stat st;
    size_t done = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st)) { int saved = errno; close(fd); errno = saved; return -1; }
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 0022U) || st.st_size <= 0 ||
        (uint64_t)st.st_size > capacity) {
        close(fd); errno = EPROTO; return -1;
    }
    while (done < (size_t)st.st_size) {
        ssize_t count = read(fd, data + done, (size_t)st.st_size - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int saved = count < 0 ? errno : EIO;
            close(fd); errno = saved; return -1;
        }
        done += (size_t)count;
    }
    for (;;) {
        unsigned char extra;
        ssize_t count = read(fd, &extra, 1U);
        if (count < 0 && errno == EINTR) continue;
        if (count != 0) {
            int saved = count < 0 ? errno : EOVERFLOW;
            close(fd); errno = saved; return -1;
        }
        break;
    }
    if (close(fd)) return -1;
    *length = done;
    return 0;
}

static int read_proc_file(const char *path, unsigned char *data, size_t capacity,
                          size_t *length)
{
    struct stat st;
    size_t done = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st)) { int saved = errno; close(fd); errno = saved; return -1; }
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 0022U)) {
        close(fd); errno = EPROTO; return -1;
    }
    for (;;) {
        ssize_t count;
        if (done == capacity) {
            unsigned char extra;
            do count = read(fd, &extra, 1U); while (count < 0 && errno == EINTR);
            if (count != 0) {
                int saved = count < 0 ? errno : EOVERFLOW;
                close(fd); errno = saved; return -1;
            }
            break;
        }
        count = read(fd, data + done, capacity - done);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { int saved = errno; close(fd); errno = saved; return -1; }
        if (!count) break;
        done += (size_t)count;
    }
    if (!done) { close(fd); errno = EIO; return -1; }
    if (close(fd)) return -1;
    *length = done;
    return 0;
}

static int parse_pid_record(struct management_identity *identity)
{
    unsigned char raw[96];
    char text[97], *first, *second;
    unsigned long long pid, starttime;
    size_t length;
    if (read_owned_file(GKD_MANAGEMENT_PID_RECORD, raw, sizeof(raw), &length) ||
        length < 4U || raw[length - 1U] != '\n' || memchr(raw, 0, length)) return -1;
    memcpy(text, raw, length); text[length - 1U] = 0;
    first = strchr(text, ' ');
    second = first ? strchr(first + 1, ' ') : NULL;
    if (!first || !second || strchr(second + 1, ' ') || first == text ||
        second == first + 1 || strcmp(second + 1, "dropbear")) {
        errno = EPROTO; return -1;
    }
    *first = 0; *second = 0;
    if (exact_unsigned(text, INT_MAX, &pid) || pid <= 1ULL ||
        exact_unsigned(first + 1, ULLONG_MAX, &starttime) || !starttime) return -1;
    identity->pid = (pid_t)pid;
    identity->starttime = starttime;
    return 0;
}

static int native_pid(pid_t expected)
{
    unsigned char raw[32];
    char text[32];
    unsigned long long pid;
    size_t length;
    if (read_owned_file(GKD_MANAGEMENT_NATIVE_PID, raw, sizeof(raw), &length) ||
        length < 2U || raw[length - 1U] != '\n' || memchr(raw, 0, length)) return -1;
    memcpy(text, raw, length); text[length - 1U] = 0;
    if (exact_unsigned(text, INT_MAX, &pid) || pid != (unsigned long long)expected) {
        errno = EPROTO; return -1;
    }
    return 0;
}

static int proc_path(char *path, size_t bytes, pid_t pid, const char *leaf)
{
    int length = snprintf(path, bytes, GKD_MANAGEMENT_PROC_ROOT "/%ld/%s",
                          (long)pid, leaf);
    if (length < 0 || (size_t)length >= bytes) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

static int proc_starttime(pid_t pid, unsigned long long *starttime)
{
    unsigned char raw[GKD_MANAGEMENT_MAX_FILE];
    char path[160], prefix[48], text[GKD_MANAGEMENT_MAX_FILE + 1U];
    char *right, *cursor;
    size_t length;
    unsigned field;
    int prefix_length;
    if (proc_path(path, sizeof(path), pid, "stat") ||
        read_proc_file(path, raw, sizeof(raw), &length) || !length ||
        memchr(raw, 0, length)) return -1;
    memcpy(text, raw, length); text[length] = 0;
    prefix_length = snprintf(prefix, sizeof(prefix), "%ld (", (long)pid);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) ||
        strncmp(text, prefix, (size_t)prefix_length)) {
        errno = EPROTO; return -1;
    }
    right = strrchr(text, ')');
    if (!right || right[1] != ' ' || !right[2] || right[3] != ' ' ||
        right[2] == 'Z' || right[2] == 'X') { errno = EPROTO; return -1; }
    cursor = right + 4;
    for (field = 4U; field <= 22U; ++field) {
        char *end = cursor;
        unsigned long long value;
        while (*end && *end != ' ' && *end != '\n') ++end;
        if (end == cursor) { errno = EPROTO; return -1; }
        if (field == 22U) {
            char saved = *end;
            *end = 0;
            if (exact_unsigned(cursor, ULLONG_MAX, &value) || !value) return -1;
            *end = saved;
            *starttime = value;
            return 0;
        }
        if (!*end || (*end != ' ' && *end != '\n')) { errno = EPROTO; return -1; }
        cursor = end + 1;
    }
    errno = EPROTO;
    return -1;
}

static int exact_proc_text(pid_t pid, const char *leaf, const char *expected)
{
    unsigned char raw[GKD_MANAGEMENT_MAX_FILE];
    char path[160];
    size_t length, expected_length = strlen(expected);
    if (proc_path(path, sizeof(path), pid, leaf) ||
        read_proc_file(path, raw, sizeof(raw), &length)) return -1;
    if (length != expected_length || memcmp(raw, expected, length)) {
        errno = EPROTO; return -1;
    }
    return 0;
}

static int exact_exe(pid_t pid)
{
    char path[160], target[256];
    ssize_t length;
    if (proc_path(path, sizeof(path), pid, "exe")) return -1;
    length = readlink(path, target, sizeof(target));
    if (length < 0) return -1;
    if ((size_t)length != sizeof(GKD_MANAGEMENT_DROPBEAR) - 1U ||
        memcmp(target, GKD_MANAGEMENT_DROPBEAR, (size_t)length)) {
        errno = EPROTO; return -1;
    }
    return 0;
}

static int exact_cmdline(pid_t pid)
{
    static const unsigned char expected[] =
        GKD_MANAGEMENT_DROPBEAR "\0-F\0-r\0" GKD_MANAGEMENT_HOST_KEY
        "\0-P\0" GKD_MANAGEMENT_NATIVE_PID "\0-p\0" GKD_MANAGEMENT_ADDRESS
        ":22\0-B\0-j\0-k\0-I\0" "600\0";
    unsigned char raw[GKD_MANAGEMENT_MAX_FILE];
    char path[160];
    size_t length;
    if (proc_path(path, sizeof(path), pid, "cmdline") ||
        read_proc_file(path, raw, sizeof(raw), &length)) return -1;
    if (length != sizeof(expected) - 1U || memcmp(raw, expected, length)) {
        errno = EPROTO; return -1;
    }
    return 0;
}

static int management_ip(void)
{
    struct ifreq request;
    struct sockaddr_in *address = (struct sockaddr_in *)&request.ifr_addr;
    int fd, result, saved;
    memset(&request, 0, sizeof(request));
    if (sizeof(GKD_MANAGEMENT_INTERFACE) > sizeof(request.ifr_name)) {
        errno = ENAMETOOLONG; return -1;
    }
    memcpy(request.ifr_name, GKD_MANAGEMENT_INTERFACE,
           sizeof(GKD_MANAGEMENT_INTERFACE));
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    result = ioctl(fd, SIOCGIFADDR, &request);
    saved = result ? errno : 0;
    if (close(fd) && !result) return -1;
    if (result) errno = saved;
    if (result || address->sin_family != AF_INET ||
        address->sin_addr.s_addr != htonl(0x0a010102UL)) {
        if (!result) errno = EADDRNOTAVAIL;
        return -1;
    }
    return 0;
}

static int listener_inode(unsigned long long *inode)
{
    char path[96], line[512];
    unsigned lines = 0, matches = 0;
    int fd, saved;
    FILE *stream;
    if (snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/net/tcp") >=
        (int)sizeof(path)) { errno = ENAMETOOLONG; return -1; }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    stream = fdopen(fd, "r");
    if (!stream) { saved = errno; close(fd); errno = saved; return -1; }
    while (fgets(line, sizeof(line), stream)) {
        char local[65], remote[65], state[8], *token, *save = NULL;
        unsigned column = 0;
        unsigned long long candidate = 0;
        if (++lines > GKD_MANAGEMENT_MAX_TCP_LINES || !strchr(line, '\n')) {
            errno = EOVERFLOW; goto fail;
        }
        if (strstr(line, "local_address")) continue;
        local[0] = remote[0] = state[0] = 0;
        for (token = strtok_r(line, " \t\n", &save); token;
             token = strtok_r(NULL, " \t\n", &save), ++column) {
            if (column == 1U) snprintf(local, sizeof(local), "%s", token);
            else if (column == 2U) snprintf(remote, sizeof(remote), "%s", token);
            else if (column == 3U) snprintf(state, sizeof(state), "%s", token);
            else if (column == 9U) {
                if (exact_unsigned(token, ULLONG_MAX, &candidate)) goto fail;
                break;
            }
        }
        if (!strcmp(local, GKD_MANAGEMENT_TCP_ADDRESS) &&
            !strcmp(remote, "00000000:0000") && !strcmp(state, "0A")) {
            if (!candidate || ++matches != 1U) { errno = EPROTO; goto fail; }
            *inode = candidate;
        }
    }
    if (ferror(stream)) goto fail;
    if (fclose(stream)) return -1;
    if (matches != 1U) { errno = EADDRNOTAVAIL; return -1; }
    return 0;
fail:
    saved = errno ? errno : EPROTO;
    fclose(stream); errno = saved; return -1;
}

static int process_owns_socket(pid_t pid, unsigned long long inode)
{
    char directory[160], expected[64];
    struct dirent *entry;
    DIR *fds;
    unsigned visited = 0, matches = 0;
    int length = snprintf(directory, sizeof(directory),
                          GKD_MANAGEMENT_PROC_ROOT "/%ld/fd", (long)pid);
    if (length < 0 || (size_t)length >= sizeof(directory)) {
        errno = ENAMETOOLONG; return -1;
    }
    length = snprintf(expected, sizeof(expected), "socket:[%llu]", inode);
    if (length < 0 || (size_t)length >= sizeof(expected)) {
        errno = EOVERFLOW; return -1;
    }
    fds = opendir(directory);
    if (!fds) return -1;
    for (;;) {
        char path[256], target[128];
        unsigned long long descriptor;
        ssize_t count;
        errno = 0;
        entry = readdir(fds);
        if (!entry) {
            if (errno) goto fail;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (exact_unsigned(entry->d_name, INT_MAX, &descriptor) ||
            ++visited > GKD_MANAGEMENT_MAX_FDS) {
            errno = EPROTO; goto fail;
        }
        length = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) { errno = ENAMETOOLONG; goto fail; }
        count = readlink(path, target, sizeof(target));
        if (count < 0) goto fail;
        if ((size_t)count == strlen(expected) && !memcmp(target, expected, (size_t)count))
            ++matches;
    }
    if (closedir(fds)) return -1;
    if (matches != 1U) { errno = EACCES; return -1; }
    return 0;
fail:
    {
        int saved = errno ? errno : EPROTO;
        closedir(fds); errno = saved; return -1;
    }
}

static int pidfd_alive(int pidfd)
{
    struct pollfd descriptor = {pidfd, POLLIN | POLLHUP | POLLERR, 0};
    int result;
    do result = GKD_MANAGEMENT_POLL(&descriptor, 1U, 0);
    while (result < 0 && errno == EINTR);
    if (result != 0) {
        if (result >= 0) errno = ESRCH;
        return -1;
    }
    return 0;
}

static int management_health(const char **failed_stage)
{
    struct management_identity identity, check, record;
    unsigned long long inode;
    int pidfd = -1, saved;
#define REQUIRE(stage, expression) do { if (expression) { *failed_stage = stage; goto fail; } } while (0)
    REQUIRE("pid-record", parse_pid_record(&identity));
    REQUIRE("native-pid", native_pid(identity.pid));
    pidfd = (int)syscall(SYS_pidfd_open, (long)identity.pid, 0UL);
    REQUIRE("pidfd", pidfd < 0 || pidfd_alive(pidfd));
    REQUIRE("starttime", proc_starttime(identity.pid, &check.starttime) ||
            check.starttime != identity.starttime);
    REQUIRE("comm", exact_proc_text(identity.pid, "comm", "dropbear\n"));
    REQUIRE("exe", exact_exe(identity.pid));
    REQUIRE("argv", exact_cmdline(identity.pid));
    REQUIRE("address", management_ip());
    REQUIRE("listener", listener_inode(&inode));
    REQUIRE("listener-owner", process_owns_socket(identity.pid, inode));
    REQUIRE("pidfd-recheck", pidfd_alive(pidfd));
    REQUIRE("identity-recheck", parse_pid_record(&record) ||
            record.pid != identity.pid || record.starttime != identity.starttime ||
            proc_starttime(identity.pid, &check.starttime) ||
            check.starttime != identity.starttime || native_pid(identity.pid) ||
            exact_proc_text(identity.pid, "comm", "dropbear\n"));
    if (close(pidfd)) { *failed_stage = "pidfd-close"; return -1; }
    return 0;
fail:
    saved = errno ? errno : EPROTO;
    if (pidfd >= 0) close(pidfd);
    errno = saved;
    return -1;
#undef REQUIRE
}

int main(int argc, char **argv)
{
    const char *stage = "arguments";
    (void)argv;
    if (argc != 1) return fail(stage, EINVAL);
#ifndef GKD_MANAGEMENT_TESTING
    if (geteuid()) return fail("owner", EPERM);
#endif
    if (management_health(&stage)) return fail(stage, errno);
    puts("GKD_APP_MANAGEMENT=READY");
    return 0;
}
