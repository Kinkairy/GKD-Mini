/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_ERROR_RECEIVER_H
#define GKD_APP_ERROR_RECEIVER_H
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

static inline int gkd_app_seqpacket_endpoint(int fd, int credentials)
{
    struct sockaddr_storage address;
    socklen_t size = sizeof(address), bytes = sizeof(int);
    int type, enabled, flags = fcntl(fd, F_GETFL), descriptor_flags = fcntl(fd, F_GETFD);
    if (flags < 0 || descriptor_flags < 0) return -1;
    if (!(flags & O_NONBLOCK) || !(descriptor_flags & FD_CLOEXEC)) goto invalid;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &bytes) ||
        getsockname(fd, (struct sockaddr *)&address, &size)) return -1;
    if (type != SOCK_SEQPACKET || address.ss_family != AF_UNIX || size != sizeof(sa_family_t))
        goto invalid;
    bytes = sizeof(enabled);
    if (getsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, &bytes)) return -1;
    if (enabled != credentials) goto invalid;
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

#endif
