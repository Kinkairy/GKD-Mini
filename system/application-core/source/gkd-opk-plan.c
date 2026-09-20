/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Derived narrowly from pcercuei/libopk opkrun.c at
 * 5cb5230c4266f866a97edd1740bc4bb0d6801038 (GPL-2.0).
 *
 * This file retains only OpenDingux metadata selection and Exec argv
 * expansion. It intentionally has no mount, loop, process, sysfs or signal
 * side effects. See UPSTREAM-PROVENANCE.md.
 */
#define _GNU_SOURCE
#include "gkd-opk-plan.h"
#include "opk.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef GKD_OPK_PLAN_TESTING
void *gkd_opk_plan_test_malloc(size_t);
void gkd_opk_plan_test_free(void *);
#define GKD_OPK_PLAN_MALLOC gkd_opk_plan_test_malloc
#define GKD_OPK_PLAN_FREE gkd_opk_plan_test_free
#endif

#ifndef GKD_OPK_PLAN_MALLOC
#define GKD_OPK_PLAN_MALLOC malloc
#endif
#ifndef GKD_OPK_PLAN_FREE
#define GKD_OPK_PLAN_FREE free
#endif

#define MOUNT_NAME_MAX 255U

static void set_default_errno(int value)
{
    if (!errno) errno = value;
}

static int exact(const char *data, size_t size, const char *literal)
{
    size_t length = strlen(literal);
    return data && size == length && !memcmp(data, literal, length);
}

static int copy_bytes(const char *data, size_t size, char **out)
{
    char *copy;
    if (!data || !out || size == SIZE_MAX || memchr(data, 0, size)) {
        errno = EPROTO;
        return -1;
    }
    copy = GKD_OPK_PLAN_MALLOC(size + 1U);
    if (!copy) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(copy, data, size);
    copy[size] = 0;
    *out = copy;
    return 0;
}

static int valid_mount_name(const char *name, size_t size)
{
    if (!name || !size || size > MOUNT_NAME_MAX || memchr(name, 0, size) ||
        memchr(name, '/', size) ||
        (size == 1U && name[0] == '.') ||
        (size == 2U && name[0] == '.' && name[1] == '.')) {
        errno = EPROTO;
        return 0;
    }
    return 1;
}

static int append_owned(struct gkd_opk_plan *plan, size_t *count, char *value)
{
    if (!value || *count >= GKD_OPK_PLAN_ARGV_MAX - 1U) {
        GKD_OPK_PLAN_FREE(value);
        errno = E2BIG;
        return -1;
    }
    plan->argv[(*count)++] = value;
    return 0;
}

static int append_copy(struct gkd_opk_plan *plan, size_t *count,
                       const char *value, size_t length)
{
    char *copy = NULL;
    if (copy_bytes(value, length, &copy)) return -1;
    return append_owned(plan, count, copy);
}

static int append_realpath(struct gkd_opk_plan *plan, size_t *count,
                           const char *value, int as_url)
{
    char *resolved = realpath(value, NULL);
    char *url = NULL;
    size_t length;
    if (!resolved) {
        if (as_url) return append_copy(plan, count, value, strlen(value));
        set_default_errno(ENOENT);
        return -1;
    }
    if (!as_url) return append_owned(plan, count, resolved);
    length = strlen(resolved);
    if (length > SIZE_MAX - sizeof("file://")) {
        free(resolved);
        errno = EOVERFLOW;
        return -1;
    }
    url = GKD_OPK_PLAN_MALLOC(length + sizeof("file://"));
    if (!url) {
        free(resolved);
        errno = ENOMEM;
        return -1;
    }
    memcpy(url, "file://", sizeof("file://") - 1U);
    memcpy(url + sizeof("file://") - 1U, resolved, length + 1U);
    free(resolved);
    return append_owned(plan, count, url);
}

static int read_metadata(const char *image_path, const char *metadata,
                         struct gkd_opk_plan *plan, char **exec_out)
{
    struct OPK *opk;
    int ret;
    int have_exec = 0, have_name = 0;
    char *exec = NULL, *name = NULL;

    opk = opk_open(image_path);
    if (!opk) {
        set_default_errno(EIO);
        return -1;
    }
    for (;;) {
        const char *filename = NULL;
        ret = opk_open_metadata(opk, &filename);
        if (ret < 0) {
            set_default_errno(EIO);
            goto fail;
        }
        if (!ret) {
            errno = ENOENT;
            goto fail;
        }
        if (!metadata || (filename && !strcmp(metadata, filename))) {
            if (!filename || strlen(filename) >= sizeof(plan->desktop)) { errno=EPROTO;goto fail; }
            strcpy(plan->desktop, filename);break;
        }
    }

    for (;;) {
        const char *key = NULL, *value = NULL;
        size_t key_size = 0, value_size = 0;
        ret = opk_read_pair(opk, &key, &key_size, &value, &value_size);
        if (ret < 0) {
            set_default_errno(EIO);
            goto fail;
        }
        if (!ret) break;
        if (!key || !value || memchr(key, 0, key_size) || memchr(value, 0, value_size)) {
            errno = EPROTO;
            goto fail;
        }
        if (exact(key, key_size, "Exec")) {
            if (have_exec || !value_size || copy_bytes(value, value_size, &exec)) {
                if (!errno) errno = EPROTO;
                goto fail;
            }
            have_exec = 1;
        } else if (exact(key, key_size, "Name")) {
            if (have_name || !valid_mount_name(value, value_size) ||
                copy_bytes(value, value_size, &name)) {
                if (!errno) errno = EPROTO;
                goto fail;
            }
            have_name = 1;
        } else if (exact(key, key_size, "Terminal")) {
            plan->needs_terminal = exact(value, value_size, "true");
        } else if (exact(key, key_size, "X-OD-NeedsJoystick")) {
            plan->needs_joystick = exact(value, value_size, "true");
        } else if (exact(key, key_size, "X-OD-NeedsGSensor")) {
            plan->needs_gsensor = exact(value, value_size, "true");
        } else if (exact(key, key_size, "X-OD-NeedsDownscaling")) {
            plan->needs_downscaling = exact(value, value_size, "true");
        }
    }
    opk_close(opk);
    if (!have_exec || !have_name) {
        GKD_OPK_PLAN_FREE(exec);
        GKD_OPK_PLAN_FREE(name);
        errno = EPROTO;
        return -1;
    }
    plan->mount_name = name;
    *exec_out = exec;
    return 0;

fail:
    opk_close(opk);
    GKD_OPK_PLAN_FREE(exec);
    GKD_OPK_PLAN_FREE(name);
    return -1;
}

static int expand_exec(char *exec, int argc, char *const input[],
                       struct gkd_opk_plan *plan)
{
    size_t count = 0;
    int used = 0;
    char *cursor = exec;

    for (;;) {
        char *next = strchr(cursor, ' ');
        if (next) {
            *next++ = 0;
            while (*next == ' ') ++next;
        }
        if (!*cursor) {
            errno = EPROTO;
            return -1;
        }
        if (!strcmp(cursor, "%f") || !strcmp(cursor, "%u")) {
            if (used >= argc) {
                errno = EINVAL;
                return -1;
            }
            if (append_realpath(plan, &count, input[used++], cursor[1] == 'u')) return -1;
        } else if (!strcmp(cursor, "%F") || !strcmp(cursor, "%U")) {
            while (used < argc) {
                if (append_realpath(plan, &count, input[used++], cursor[1] == 'U')) return -1;
            }
        } else if (append_copy(plan, &count, cursor, strlen(cursor))) {
            return -1;
        }
        if (!next) break;
        cursor = next;
    }
    if (used != argc) {
        errno = E2BIG;
        return -1;
    }
    if (!count) {
        errno = EPROTO;
        return -1;
    }
    plan->argv[count] = NULL;
    return 0;
}

int gkd_opk_plan_open(const char *image_path, const char *metadata,
                      int argc, char *const input[],
                      struct gkd_opk_plan *out)
{
    struct gkd_opk_plan plan = {0};
    char *exec = NULL;

    if (!image_path || !*image_path || !out || argc < 0 || (argc && !input)) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < argc; ++i) {
        if (!input[i]) {
            errno = EINVAL;
            return -1;
        }
    }
    if (read_metadata(image_path, metadata, &plan, &exec) ||
        expand_exec(exec, argc, input, &plan)) {
        int saved = errno;
        GKD_OPK_PLAN_FREE(exec);
        gkd_opk_plan_close(&plan);
        errno = saved;
        return -1;
    }
    GKD_OPK_PLAN_FREE(exec);
    *out = plan;
    return 0;
}

void gkd_opk_plan_close(struct gkd_opk_plan *out)
{
    if (!out) return;
    for (size_t i = 0; i < GKD_OPK_PLAN_ARGV_MAX; ++i) {
        GKD_OPK_PLAN_FREE(out->argv[i]);
        out->argv[i] = NULL;
    }
    GKD_OPK_PLAN_FREE(out->mount_name);
    out->mount_name = NULL;
    out->needs_terminal = 0;
    out->needs_joystick = 0;
    out->needs_gsensor = 0;
    out->needs_downscaling = 0;
    out->desktop[0] = 0;
}

