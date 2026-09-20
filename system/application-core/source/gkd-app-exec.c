/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-exec.h"
#include "gkd-app-output.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ITEM_MAX 64U
#define STRING_MAX 4096U
#define TOTAL_MAX 16384U
static const unsigned char error_magic[8] = {'G','K','D','E','X','E','1',0};

int gkd_app_exec_error_decode(const void *data, size_t length, int *error)
{
    const unsigned char *packet = data;
    unsigned int value;
    if (!packet || !error || length != GKD_APP_EXEC_ERROR_BYTES ||
        memcmp(packet, error_magic, sizeof(error_magic))) { errno = EPROTO; return -1; }
    value = ((unsigned int)packet[8] << 24) | ((unsigned int)packet[9] << 16) |
            ((unsigned int)packet[10] << 8) | packet[11];
    /* Linux syscall error range, including the MIPS errno numbering. */
    if (!value || value > 4095U) { errno = EPROTO; return -1; }
    *error = (int)value;
    return 0;
}

static int vector_size(char *const *values, size_t *count, int environment)
{
    size_t total = 0, i, j;
    if (!values) return -1;
    for (i = 0; i < ITEM_MAX && values[i]; ++i) {
        size_t length = strnlen(values[i], STRING_MAX);
        if (!length || length == STRING_MAX || (total += length + 1) > TOTAL_MAX) return -1;
        if (environment) {
            const char *eq = strchr(values[i], '=');
            if (!eq || eq == values[i]) return -1;
            for (const char *c = values[i]; c < eq; ++c)
                if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                    *c == '_' || (c != values[i] && *c >= '0' && *c <= '9'))) return -1;
            if (!strncmp(values[i], "GKD_APP_", 8) ||
                (!strncmp(values[i], "LD_", 3) && strncmp(values[i], "LD_LIBRARY_PATH=", 16)))
                return -1;
            for (j = 0; j < i; ++j)
                if (!strncmp(values[j], values[i], (size_t)(eq - values[i]) + 1)) return -1;
        }
    }
    if (i == ITEM_MAX) return -1;
    *count = i;
    return 0;
}

static int validate(const struct gkd_app_exec_request *r, size_t *env_count)
{
    struct stat executable, terminal;
    struct sockaddr_storage address;
    unsigned char signature[4];
    socklen_t length = sizeof(address), type_length = sizeof(int);
    int type;
    size_t argc, preload_length;
    unsigned int any = 0, i;
    if (!r || r->preparer_pid <= 0 || r->preparer_pid == getpid() ||
        getpid() == 1 || getppid() != r->preparer_pid) { errno = EPERM; return -1; }
    if (!r->launch_token || !r->preload_path || r->preload_path[0] != '/' ||
        vector_size(r->argv, &argc, 0) || !argc || vector_size(r->base_env, env_count, 1))
        goto invalid;
    preload_length = strnlen(r->preload_path, STRING_MAX);
    if (preload_length == STRING_MAX || strpbrk(r->preload_path, ": \t\r\n")) goto invalid;
    for (i = 0; i < 16; ++i) any |= r->launch_token[i];
    if (!any || r->executable_fd < 0 || r->terminal_fd < 0 || r->ready_fd < 3 ||
        r->output_fd < 3 || r->executable_fd == r->terminal_fd ||
        r->executable_fd == r->output_fd || r->executable_fd == r->ready_fd ||
        r->terminal_fd == r->output_fd || r->terminal_fd == r->ready_fd ||
        r->output_fd == r->ready_fd || gkd_app_output_validate(r->output_fd)) goto invalid;
    if (fstat(r->executable_fd, &executable) || fstat(r->terminal_fd, &terminal)) return -1;
    if (!S_ISREG(executable.st_mode) || !(executable.st_mode & 0111) ||
        (executable.st_mode & (S_ISUID | S_ISGID)) || !S_ISCHR(terminal.st_mode)) goto invalid;
    if (pread(r->executable_fd, signature, sizeof(signature), 0) != (ssize_t)sizeof(signature) ||
        memcmp(signature, "\177ELF", sizeof(signature))) { errno = ENOEXEC; return -1; }
    if (getsockname(r->ready_fd, (struct sockaddr *)&address, &length) ||
        getsockopt(r->ready_fd, SOL_SOCKET, SO_TYPE, &type, &type_length)) return -1;
    if (address.ss_family != AF_UNIX || type != SOCK_DGRAM) goto invalid;
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

static int replace(const struct gkd_app_exec_request *r, int error_fd)
{
    char preload[STRING_MAX + 12], token[sizeof("GKD_APP_READY_TOKEN=") + 32];
    char sender[] = "GKD_APP_READY_FD=3";
    char *environment[ITEM_MAX + 4];
    static const char digits[] = "0123456789abcdef";
    int executable = -1, terminal = -1, output = -1, ready = -1, report = -1;
    int fixed = 0, output_valid = 0, output_mapped = 0, saved;
    size_t env_count, i, prefix = sizeof("GKD_APP_READY_TOKEN=") - 1;
    if (validate(r, &env_count)) goto failed;
    output_valid = 1;
    (void)snprintf(preload, sizeof(preload), "LD_PRELOAD=%s", r->preload_path);
    memcpy(token, "GKD_APP_READY_TOKEN=", prefix);
    for (i = 0; i < 16; ++i) {
        token[prefix + 2*i] = digits[r->launch_token[i] >> 4];
        token[prefix + 2*i + 1] = digits[r->launch_token[i] & 15];
    }
    token[prefix + 32] = 0;
    for (i = 0; i < env_count; ++i) environment[i] = r->base_env[i];
    environment[i++] = preload; environment[i++] = sender; environment[i++] = token;
    environment[i] = NULL;

    /* Stage all inputs above the fixed range before overwriting any descriptor.
     * This works even when original inputs already occupy slots 3 and 4.
     */
    if (error_fd >= 0) {
        report = fcntl(error_fd, F_DUPFD_CLOEXEC, 8);
        if (report < 0) goto failed;
    }
    executable = fcntl(r->executable_fd, F_DUPFD_CLOEXEC, 8);
    if (executable < 0) goto failed;
    terminal = fcntl(r->terminal_fd, F_DUPFD_CLOEXEC, 8);
    if (terminal < 0) goto failed;
    output = fcntl(r->output_fd, F_DUPFD_CLOEXEC, 8);
    if (output < 0) goto failed;
    ready = fcntl(r->ready_fd, F_DUPFD_CLOEXEC, 8);
    if (ready < 0) goto failed;
    if (dup3(terminal, 0, 0) < 0 || dup3(output, 1, 0) < 0 || dup3(output, 2, 0) < 0) goto failed;
    output_mapped = 1;
    if (dup3(ready, 3, 0) < 0 || dup3(executable, 4, O_CLOEXEC) < 0) goto failed;
    if (report >= 0) {
        if (dup3(report, 5, O_CLOEXEC) < 0) goto failed;
        fixed = 1;
    }
    if (syscall(SYS_close_range, fixed ? 6U : 5U, ~0U, 0U) < 0) goto failed;
    executable = terminal = output = ready = -1;
    report = -1;
    (void)syscall(SYS_execveat, 4, "", r->argv, environment, AT_EMPTY_PATH);
failed:
    saved = errno;
    if (output_valid) gkd_app_output_stage(output_mapped ? 2 : r->output_fd,
                                            "EXEC_FAILURE", saved, 0, -1);
    if (error_fd >= 0) {
        unsigned char packet[GKD_APP_EXEC_ERROR_BYTES];
        unsigned int value = (unsigned int)saved;
        memcpy(packet, error_magic, sizeof(error_magic));
        packet[8] = (unsigned char)(value >> 24);
        packet[9] = (unsigned char)(value >> 16);
        packet[10] = (unsigned char)(value >> 8);
        packet[11] = (unsigned char)value;
        (void)send(fixed ? 5 : (report >= 0 ? report : error_fd), packet,
                   sizeof(packet), MSG_DONTWAIT | MSG_NOSIGNAL);
    }
    if (executable >= 0) (void)close(executable);
    if (terminal >= 0) (void)close(terminal);
    if (output >= 0) (void)close(output);
    if (ready >= 0) (void)close(ready);
    if (report >= 0) (void)close(report);
    if (fixed) (void)close(5);
    errno = saved;
    return -1;
}

int gkd_app_exec_replace(const struct gkd_app_exec_request *r)
{
    return replace(r, -1);
}

int gkd_app_exec_replace_report(const struct gkd_app_exec_request *r, int fd)
{
    struct sockaddr_storage address;
    socklen_t length = sizeof(address), type_length = sizeof(int);
    int flags, descriptor_flags, type;
    /* Never send from the preparer, even if another request field is invalid. */
    if (!r || r->preparer_pid <= 0 || r->preparer_pid == getpid() ||
        getpid() == 1 || getppid() != r->preparer_pid) { errno = EPERM; return -1; }
    if (fd < 3 || fd == r->executable_fd || fd == r->terminal_fd ||
        fd == r->output_fd || fd == r->ready_fd)
        goto invalid;
    flags = fcntl(fd, F_GETFL);
    descriptor_flags = fcntl(fd, F_GETFD);
    if (flags < 0 || descriptor_flags < 0) return -1;
    if (!(flags & O_NONBLOCK) || !(descriptor_flags & FD_CLOEXEC)) goto invalid;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) ||
        getsockname(fd, (struct sockaddr *)&address, &length)) return -1;
    if (type != SOCK_SEQPACKET || address.ss_family != AF_UNIX ||
        length != sizeof(sa_family_t)) goto invalid;
    length = sizeof(address);
    if (getpeername(fd, (struct sockaddr *)&address, &length)) return -1;
    if (address.ss_family != AF_UNIX || length != sizeof(sa_family_t)) goto invalid;
    return replace(r, fd);
invalid:
    errno = EINVAL;
    return -1;
}
