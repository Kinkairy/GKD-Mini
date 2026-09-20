/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fixture_poll(struct pollfd *fds, nfds_t count, int timeout);
static void text(const char *path, const char *value);
#define GKD_MANAGEMENT_TESTING 1
#define GKD_MANAGEMENT_RUN_ROOT "/tmp/gkd-management-fixture/run"
#define GKD_MANAGEMENT_PROC_ROOT "/tmp/gkd-management-fixture/proc"
#define GKD_MANAGEMENT_POLL fixture_poll
#define main gkd_app_management_main
#include "../source/gkd-app-management.c"
#undef main

static const pid_t fixture_pid = 320;
static const unsigned long long fixture_starttime = 307ULL;
static uint32_t fixture_ip = 0x0a010102UL;
static int pin[2] = {-1, -1};
static unsigned poll_calls;
static int die_on_second_poll;
static int change_record_on_second_poll;

long __wrap_syscall(long number, ...)
{
    va_list arguments;
    long pid;
    unsigned long flags;
    assert(number == SYS_pidfd_open);
    va_start(arguments, number);
    pid = va_arg(arguments, long);
    flags = va_arg(arguments, unsigned long);
    va_end(arguments);
    assert(pid == fixture_pid && flags == 0U && pin[0] >= 0);
    return dup(pin[0]);
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    struct ifreq *interface;
    struct sockaddr_in *address;
    va_list arguments;
    (void)fd;
    assert(request == SIOCGIFADDR);
    va_start(arguments, request);
    interface = va_arg(arguments, struct ifreq *);
    va_end(arguments);
    assert(!strcmp(interface->ifr_name, "usb0"));
    address = (struct sockaddr_in *)&interface->ifr_addr;
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(fixture_ip);
    return 0;
}

static int fixture_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    ++poll_calls;
    if (die_on_second_poll && poll_calls == 2U && pin[1] >= 0) {
        close(pin[1]); pin[1] = -1;
    }
    if (change_record_on_second_poll && poll_calls == 2U)
        text(GKD_MANAGEMENT_PID_RECORD, "320 308 dropbear\n");
    return poll(fds, count, timeout);
}

static void directory(const char *path)
{
    assert(!mkdir(path, 0700));
}

static void bytes(const char *path, const void *data, size_t length)
{
    size_t done = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    assert(fd >= 0);
    while (done < length) {
        ssize_t count = write(fd, (const unsigned char *)data + done, length - done);
        assert(count > 0);
        done += (size_t)count;
    }
    assert(!close(fd));
}

static void text(const char *path, const char *value)
{
    bytes(path, value, strlen(value));
}

static void process_stat(unsigned long long starttime)
{
    char path[160];
    int fd;
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/stat", (long)fixture_pid);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    assert(fd >= 0);
    assert(dprintf(fd, "%ld (dropbear) S", (long)fixture_pid) > 0);
    for (unsigned field = 4U; field <= 21U; ++field) assert(dprintf(fd, " 1") == 2);
    assert(dprintf(fd, " %llu 0 0 0\n", starttime) > 0);
    assert(!close(fd));
}

static void tcp_listener(const char *local, const char *state,
                         unsigned long long inode)
{
    char line[1024], path[160];
    int length = snprintf(line, sizeof(line),
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode\n"
        "   0: %s 00000000:0000 %s 00000000:00000000 00:00000000 00000000 0 0 %llu 1 00000000 100 0 0 10 0\n",
        local, state, inode);
    assert(length > 0 && (size_t)length < sizeof(line));
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/net/tcp");
    text(path, line);
}

static void command_line(int valid)
{
    static const unsigned char expected[] =
        GKD_MANAGEMENT_DROPBEAR "\0-F\0-r\0" GKD_MANAGEMENT_HOST_KEY
        "\0-P\0" GKD_MANAGEMENT_NATIVE_PID "\0-p\0" GKD_MANAGEMENT_ADDRESS
        ":22\0-B\0-j\0-k\0-I\0" "600\0";
    char path[160];
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/cmdline", (long)fixture_pid);
    if (valid) bytes(path, expected, sizeof(expected) - 1U);
    else {
        static const unsigned char invalid[] =
            "/usr/sbin/dropbear\0-F\0-p\00.0.0.0:22\0";
        bytes(path, invalid, sizeof(invalid) - 1U);
    }
}

static void duplicate_listener(void)
{
    char line[1400], path[160];
    int length = snprintf(line, sizeof(line),
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode\n"
        "   0: %s 00000000:0000 0A 00000000:00000000 00:00000000 00000000 0 0 1253 1 00000000 100 0 0 10 0\n"
        "   1: %s 00000000:0000 0A 00000000:00000000 00:00000000 00000000 0 0 1254 1 00000000 100 0 0 10 0\n",
        GKD_MANAGEMENT_TCP_ADDRESS, GKD_MANAGEMENT_TCP_ADDRESS);
    assert(length > 0 && (size_t)length < sizeof(line));
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/net/tcp");
    text(path, line);
}

static void reset_valid(void)
{
    char path[200];
    assert(system("rm -rf -- /tmp/gkd-management-fixture") == 0);
    directory("/tmp/gkd-management-fixture");
    directory(GKD_MANAGEMENT_RUN_ROOT);
    directory(GKD_MANAGEMENT_PROC_ROOT);
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/net"); directory(path);
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld", (long)fixture_pid); directory(path);
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/fd", (long)fixture_pid); directory(path);
    text(GKD_MANAGEMENT_PID_RECORD, "320 307 dropbear\n");
    text(GKD_MANAGEMENT_NATIVE_PID, "320\n");
    process_stat(fixture_starttime);
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/comm", (long)fixture_pid);
    text(path, "dropbear\n");
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/exe", (long)fixture_pid);
    assert(!symlink(GKD_MANAGEMENT_DROPBEAR, path));
    command_line(1);
    tcp_listener(GKD_MANAGEMENT_TCP_ADDRESS, "0A", 1253ULL);
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/fd/5", (long)fixture_pid);
    assert(!symlink("socket:[1253]", path));
    fixture_ip = 0x0a010102UL;
    poll_calls = 0U; die_on_second_poll = 0; change_record_on_second_poll = 0;
    if (pin[0] >= 0) close(pin[0]);
    if (pin[1] >= 0) close(pin[1]);
    assert(!pipe2(pin, O_CLOEXEC));
}

static int check(const char *wanted_stage)
{
    const char *stage = NULL;
    int result = management_health(&stage);
    if (!wanted_stage) return result == 0;
    if (!(result < 0 && stage && !strcmp(stage, wanted_stage)))
        fprintf(stderr, "wanted=%s actual=%s result=%d errno=%d polls=%u\n",
                wanted_stage, stage ? stage : "none", result, errno, poll_calls);
    return result < 0 && stage && !strcmp(stage, wanted_stage);
}

int main(void)
{
    char path[200], saved[220];
    char *argv[] = {"gkd-app-management", NULL};
    reset_valid();
    assert(check(NULL));
    assert(gkd_app_management_main(1, argv) == 0);

    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/stat", (long)fixture_pid);
    assert(!unlink(path)); assert(check("starttime")); process_stat(fixture_starttime);
    process_stat(fixture_starttime + 1ULL); assert(check("starttime")); process_stat(fixture_starttime);

    text(GKD_MANAGEMENT_PID_RECORD, "320  307 dropbear\n"); assert(check("pid-record"));
    text(GKD_MANAGEMENT_PID_RECORD, "320 307 dropbear\n");
    snprintf(saved, sizeof(saved), "%s.real", GKD_MANAGEMENT_PID_RECORD);
    assert(!rename(GKD_MANAGEMENT_PID_RECORD, saved));
    assert(!symlink(saved, GKD_MANAGEMENT_PID_RECORD)); assert(check("pid-record"));
    assert(!unlink(GKD_MANAGEMENT_PID_RECORD)); assert(!rename(saved, GKD_MANAGEMENT_PID_RECORD));

    text(GKD_MANAGEMENT_NATIVE_PID, "321\n"); assert(check("native-pid"));
    text(GKD_MANAGEMENT_NATIVE_PID, "320\n");
    snprintf(saved, sizeof(saved), "%s.real", GKD_MANAGEMENT_NATIVE_PID);
    assert(!rename(GKD_MANAGEMENT_NATIVE_PID, saved));
    assert(!symlink(saved, GKD_MANAGEMENT_NATIVE_PID)); assert(check("native-pid"));
    assert(!unlink(GKD_MANAGEMENT_NATIVE_PID)); assert(!rename(saved, GKD_MANAGEMENT_NATIVE_PID));
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/comm", (long)fixture_pid);
    text(path, "not-dropbear\n"); assert(check("comm")); text(path, "dropbear\n");
    snprintf(path, sizeof(path), GKD_MANAGEMENT_PROC_ROOT "/%ld/exe", (long)fixture_pid);
    assert(!unlink(path)); assert(!symlink("/tmp/foreign-dropbear", path)); assert(check("exe"));
    assert(!unlink(path)); assert(!symlink(GKD_MANAGEMENT_DROPBEAR, path));
    command_line(0); assert(check("argv")); command_line(1);

    fixture_ip = 0x0a010103UL; assert(check("address")); fixture_ip = 0x0a010102UL;
    tcp_listener("0301010A:0016", "0A", 1253ULL); assert(check("listener"));
    tcp_listener(GKD_MANAGEMENT_TCP_ADDRESS, "01", 1253ULL); assert(check("listener"));
    duplicate_listener(); assert(check("listener"));
    tcp_listener(GKD_MANAGEMENT_TCP_ADDRESS, "0A", 1254ULL); assert(check("listener-owner"));
    tcp_listener(GKD_MANAGEMENT_TCP_ADDRESS, "0A", 1253ULL);

    die_on_second_poll = 1; poll_calls = 0U; assert(check("pidfd-recheck"));
    reset_valid(); change_record_on_second_poll = 1; assert(check("identity-recheck"));
    assert(system("rm -rf -- /tmp/gkd-management-fixture") == 0);
    if (pin[0] >= 0) close(pin[0]);
    if (pin[1] >= 0) close(pin[1]);
    puts("GKD_APP_MANAGEMENT_FIXTURE=PASS pid/starttime/native/comm/exe/argv/ip/listener/inode/pidfd/symlink");
    return 0;
}
