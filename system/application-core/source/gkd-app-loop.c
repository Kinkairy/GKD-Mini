/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-loop.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef GKD_APP_LOOP_DEVICE_ROOT
#define GKD_APP_LOOP_DEVICE_ROOT "/dev"
#endif

/* Linux dev_t encoding from the pinned toolchain sys/sysmacros.h.
 * Keep it local: P1 uClibc 0.9.33.2 does not export gnu_dev_major/minor. */
static unsigned device_major(dev_t dev)
{
    uint64_t value = (uint64_t)dev;
    return ((value >> 8) & 0xfff) | ((unsigned)(value >> 32) & ~0xfffU);
}
static unsigned device_minor(dev_t dev)
{
    uint64_t value = (uint64_t)dev;
    return (value & 0xff) | ((unsigned)(value >> 12) & ~0xffU);
}

/* Pinned loop-aes driver: internal READ_ONLY=2, exported read-only=0x200000. */
#define GKD_LOOP_READ_ONLY 0x200002U
static int readonly_mapping(const struct loop_info64 *i)
{ return (i->lo_flags & GKD_LOOP_READ_ONLY) == GKD_LOOP_READ_ONLY; }

static int owner_valid(const char *owner)
{
    if (!owner || strlen(owner) != 40 || memcmp(owner, "gkd-app-", 8)) return 0;
    for (unsigned i = 8; i < 40; ++i)
        if (!((owner[i] >= '0' && owner[i] <= '9') ||
              (owner[i] >= 'a' && owner[i] <= 'f'))) return 0;
    return 1;
}

static int loop_open(unsigned index)
{
    char path[256];
    struct stat st;
    int n = snprintf(path, sizeof(path), GKD_APP_LOOP_DEVICE_ROOT "/loop%u", index);
    if (n < 0 || (size_t)n >= sizeof(path)) { errno = ENAMETOOLONG; return -1; }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st)) {
        int saved = errno;
        close(fd); errno = saved; return -1;
    }
    if (!S_ISBLK(st.st_mode) || device_major(st.st_rdev) != 7 || device_minor(st.st_rdev) != index) {
        close(fd); errno = ENOTBLK; return -1;
    }
    return fd;
}

/* loop_info64 uses Linux new_encode_dev, independent of libc dev_t layout. */
static uint64_t device_id(dev_t dev)
{
    uint64_t low = device_minor(dev);
    return (low & 0xff) | ((uint64_t)device_major(dev) << 8) | ((low & ~UINT64_C(0xff)) << 12);
}

static int owned(const struct loop_info64 *info, const char *owner)
{
    return !memcmp(info->lo_file_name, owner, 41);
}

static int detach(int fd)
{
    struct loop_info64 after;
    if (ioctl(fd, LOOP_CLR_FD, 0)) return -1;
    memset(&after, 0, sizeof(after));
    if (ioctl(fd, LOOP_GET_STATUS64, &after) < 0 && errno == ENXIO) return 0;
    errno = EBUSY;
    return -1;
}

int gkd_app_loop_mount_owned(int image_fd, const char *target, const char *owner,
                             struct gkd_app_loop *lease)
{
    if (!lease || lease->fd != -1) { errno = EINVAL; return -1; }
    struct stat image;
    if (!owner_valid(owner) || !target || target[0] != '/') { errno = EINVAL; return -1; }
    if (fstat(image_fd, &image)) return -1;
    int flags = fcntl(image_fd, F_GETFL);
    if (flags < 0) return -1;
    if (!S_ISREG(image.st_mode) || (flags & O_ACCMODE) != O_RDONLY) {
        errno = EINVAL; return -1;
    }
    for (unsigned index = 0; index < 8; ++index) {
        int fd = loop_open(index);
        if (fd < 0) {
            if (errno == ENOENT || errno == ENXIO) continue;
            return -1;
        }
        struct loop_info64 info;
        memset(&info, 0, sizeof(info));
        if (!ioctl(fd, LOOP_GET_STATUS64, &info)) { close(fd); continue; }
        if (errno != ENXIO) { int saved = errno; close(fd); errno = saved; return -1; }
        if (ioctl(fd, LOOP_SET_FD, image_fd)) {
            int saved = errno;
            close(fd);
            if (saved == EBUSY) continue;
            errno = saved; return -1;
        }
        lease->fd = fd; lease->number = index; lease->unlabelled = 1;
        lease->device = device_id(image.st_dev); lease->inode = image.st_ino;
        memcpy(lease->owner, owner, 41);
        /* SET_FD succeeded: only this call owns the attachment. The pinned
         * driver gets READ_ONLY from image_fd, and ignores AUTOCLEAR flags. */
        memset(&info, 0, sizeof(info));
        info.lo_flags = 0; /* Pinned SET_STATUS ignores flags; SET_FD uses O_RDONLY. */
        memcpy(info.lo_file_name, owner, 41);
        int result = ioctl(fd, LOOP_SET_STATUS64, &info);
        if (!result) lease->unlabelled = 0;
        if (!result) {
            memset(&info, 0, sizeof(info));
            result = ioctl(fd, LOOP_GET_STATUS64, &info);
            if (!result && (!owned(&info, owner) || !readonly_mapping(&info) ||
                info.lo_number != index || info.lo_device != device_id(image.st_dev) || info.lo_inode != image.st_ino ||
                info.lo_offset || info.lo_sizelimit || info.lo_encrypt_type)) {
                errno = EBADMSG; result = -1;
            }
        }
        if (!result) {
            char device[256];
            snprintf(device, sizeof(device), GKD_APP_LOOP_DEVICE_ROOT "/loop%u", index);
            result = mount(device, target, "squashfs", MS_RDONLY | MS_NOSUID | MS_NODEV, NULL);
        }
        int saved = errno;
        if (result) {
            if (detach(fd)) {
                saved = errno;
                fprintf(stderr, "GKD_APP_LOOP=detach-failed lease-retained=1 index=%u errno=%d\n", index, saved);
            } else { close(fd); lease->fd = -1; }
        }
        if (!result) {
            lease->fd = fd; lease->number = index;
            lease->device = device_id(image.st_dev); lease->inode = image.st_ino;
            memcpy(lease->owner, owner, 41);
            fprintf(stderr, "GKD_APP_LOOP=mounted index=%u\n", index);
        }
        errno = saved;
        return result;
    }
    errno = EBUSY;
    return -1;
}

int gkd_app_loop_cleanup(const char *owner)
{
    if (!owner_valid(owner)) { errno = EINVAL; return -1; }
    int failure = 0;
    for (unsigned index = 0; index < 8; ++index) {
        int fd = loop_open(index);
        if (fd < 0) {
            if (errno != ENOENT && errno != ENXIO && !failure) failure = errno;
            continue;
        }
        struct loop_info64 info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, LOOP_GET_STATUS64, &info)) {
            if (errno != ENXIO && !failure) failure = errno;
        } else if (owned(&info, owner)) {
            if (!readonly_mapping(&info) || info.lo_offset ||
                info.lo_sizelimit || info.lo_encrypt_type) {
                if (!failure) failure = EBADMSG;
            } else if (detach(fd)) {
                int saved = errno;
                if (!failure) failure = saved;
                fprintf(stderr, "GKD_APP_LOOP=cleanup-failed index=%u errno=%d\n", index, saved);
            } else {
                fprintf(stderr, "GKD_APP_LOOP=detached index=%u\n", index);
            }
        }
        close(fd);
    }
    if (failure) { errno = failure; return -1; }
    return 0;
}

int gkd_app_loop_release(struct gkd_app_loop *lease)
{
    struct loop_info64 info;
    if (!lease || lease->fd < 0 || !owner_valid(lease->owner)) { errno = EINVAL; return -1; }
    memset(&info, 0, sizeof(info));
    if (ioctl(lease->fd, LOOP_GET_STATUS64, &info)) return -1;
    if ((!owned(&info, lease->owner) && !(lease->unlabelled && !info.lo_file_name[0])) || !readonly_mapping(&info) ||
        info.lo_number != lease->number || info.lo_device != lease->device ||
        info.lo_inode != lease->inode || info.lo_offset || info.lo_sizelimit ||
        info.lo_encrypt_type) { errno = EBADMSG; return -1; }
    if (detach(lease->fd)) return -1;
    close(lease->fd); lease->fd = -1;
    return 0;
}

int gkd_app_loop_mount(int image_fd, const char *target, const char *owner)
{
    struct gkd_app_loop lease = GKD_APP_LOOP_INIT;
    int rc = gkd_app_loop_mount_owned(image_fd, target, owner, &lease);
    /* The host releases its namespace first, then cleans its exact owner. */
    if (lease.fd >= 0) close(lease.fd);
    return rc;
}

int gkd_app_loop_owned_count(const char *owner)
{
    if (!owner_valid(owner)) { errno = EINVAL; return -1; }
    int count = 0;
    for (unsigned i = 0; i < 8; i++) {
        int fd = loop_open(i);
        if (fd < 0) {
            if (errno == ENOENT || errno == ENXIO) continue;
            return -1;
        }
        struct loop_info64 info;
        memset(&info, 0, sizeof(info));
        int rc = ioctl(fd, LOOP_GET_STATUS64, &info), saved = errno;
        close(fd);
        if (rc) { if (saved == ENXIO) continue; errno = saved; return -1; }
        if (owned(&info, owner)) count++;
    }
    return count;
}
