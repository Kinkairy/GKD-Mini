// SPDX-License-Identifier: GPL-2.0
/* Reuse the exact accepted A backend; only R lifecycle ownership differs. */
#define main gkd_accepted_poweroff_entry
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-result"
#include "../../service-core/source/gkd-pmic-poweroff.c"
#pragma GCC diagnostic pop
#undef main
#include <glob.h>

#ifndef GKD_R_POWER_TEST
#define POWER_PATH(path) path
#else
extern const char *gkd_power_test_path(const char *path);
#define POWER_PATH(path) gkd_power_test_path(path)
#endif

static int r_offline(void)
{
    char line[768], source[160], target[160], type[48], options[320];
    FILE *stream;
    glob_t luns;
    size_t i;
    int root_ram = 0, result, fd;
    stream = fopen(POWER_PATH("/proc/self/mounts"), "r");
    if (!stream) return -1;
    while (fgets(line, sizeof(line), stream)) {
        if (sscanf(line, "%159s %159s %47s %319s", source, target, type, options) != 4) {
            fclose(stream); return -1;
        }
        if (!strcmp(target, "/")) root_ram = !strcmp(source, "rootfs") && !strcmp(type, "rootfs");
        if ((!strncmp(source, "/dev/mmcblk", 11) || !strcmp(source, "/dev/root")) && option_is_rw(options)) {
            fclose(stream); return -1;
        }
    }
    result = ferror(stream); fclose(stream);
    if (result || !root_ram) return -1;
    stream = fopen(POWER_PATH("/proc/swaps"), "r");
    if (!stream) return -1;
    if (!fgets(line, sizeof(line), stream) || strncmp(line, "Filename", 8)) {
        fclose(stream); return -1;
    }
    result = fgets(line, sizeof(line), stream) != NULL || ferror(stream);
    fclose(stream);
    if (result) return -1;
    memset(&luns, 0, sizeof(luns));
    result = glob(POWER_PATH("/sys/kernel/config/usb_gadget/*/functions/mass_storage.*/lun.*/file"), 0, NULL, &luns);
    if (result != 0 && result != GLOB_NOMATCH) { globfree(&luns); return -1; }
    for (i = 0; i < luns.gl_pathc; ++i) {
        char value[2];
        ssize_t count;
        fd = open(luns.gl_pathv[i], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { globfree(&luns); return -1; }
        count = read(fd, value, sizeof(value)); close(fd);
        if (count < 0 || (count != 0 && !(count == 1 && value[0] == '\n'))) {
            globfree(&luns); return -1;
        }
    }
    globfree(&luns);
    return 0;
}

int gkd_r_poweroff(void)
{
    int fd = -1, result = -1;
    const char *stage = "offline";
    struct timespec delay = {2, 0};
    struct stat state;
    if (mkdir(POWER_PATH("/run/gkd-recovery"), 0755) < 0 && errno != EEXIST) return -1;
    if (lstat(POWER_PATH("/run/gkd-recovery"), &state) < 0 || !S_ISDIR(state.st_mode)) return -1;
    if (mkdir(POWER_PATH("/run/gkd-recovery/card-operation.lock"), 0700) < 0) return -1;
    if (r_offline() < 0) goto out;
    stage = "probe";
    fd = open(POWER_PATH(I2C_DEVICE), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || probe_axp173(fd) < 0) goto out;
    /* No process sweep or USB stop: failures must leave UI/input/SSH alive. */
#ifndef GKD_R_POWER_TEST
    sync();
#endif
    stage = "recheck";
    if (r_offline() < 0) goto out;
    stage = "cutoff-write";
    if (request_axp173_poweroff(fd) < 0) goto out;
#ifndef GKD_R_POWER_TEST
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
#else
    (void)delay;
#endif
    /* A successful cutoff never returns. Never fall through to kernel halt. */
    errno = EIO;
    stage = "cutoff-returned";
out:
    {
        char message[160];
        int saved_errno = errno;
        int log_fd = open(POWER_PATH("/run/gkd-recovery/power.log"),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        int length = snprintf(message, sizeof(message), "GKD_R_POWER=FAILED stage=%s errno=%d\n", stage, saved_errno);
        if (log_fd >= 0) {
            ssize_t logged = write(log_fd, message, (size_t)length);
            (void)logged;
            close(log_fd);
        }
    }
    if (fd >= 0) close(fd);
    if (rmdir(POWER_PATH("/run/gkd-recovery/card-operation.lock")) < 0) return -1;
    return result;
}
