/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui.h"
#include "gkd-ui-plane-client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int number) { (void)number; stopped = 1; }

static int number(const char *text, unsigned maximum, unsigned *value)
{
    unsigned parsed = 0U;
    if (!text || !*text) return -1;
    for (const char *p = text; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9') return -1;
        digit = (unsigned)(*p - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10U) return -1;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return 0;
}

int main(int argc, char **argv)
{
    struct gkd_ui_config config;
    struct gkd_ui_font font;
    struct gkd_ui_osd osd;
    struct gkd_ui_plane_caps caps;
    struct sigaction action;
    struct stat state;
    uint32_t *tile;
    unsigned icon, level = 0U, critical, ttl, fade;
    int fd = -1, result = 1, waited;
    if (argc != 8 || number(argv[2], GKD_UI_OSD_ICON_COUNT - 1U, &icon) ||
        (strcmp(argv[3], "-1") && number(argv[3], 100U, &level)) ||
        number(argv[4], 1U, &critical) ||
        number(argv[6], GKD_UI_PLANE_MAX_TTL_MS, &ttl) ||
        ttl < GKD_UI_PLANE_MIN_TTL_MS ||
        number(argv[7], GKD_UI_PLANE_MAX_FADE_MS, &fade)) {
        fputs("usage: gkd-ui-plane-probe FONT ICON LEVEL|-1 CRITICAL TEXT TTL_MS FADE_MS\n", stderr);
        return 2;
    }
    osd = (struct gkd_ui_osd){argv[5], icon,
        !strcmp(argv[3], "-1") ? -1 : (int)level, (int)critical};
    gkd_ui_config_defaults(&config);
    if (gkd_ui_font_load(&font, argv[1])) { perror("font"); return 1; }
    tile = calloc(GKD_UI_PLANE_PIXELS, sizeof(*tile));
    if (!tile || gkd_ui_export_osd_argb(tile, GKD_UI_PLANE_PIXELS,
                                      &config, &font, &osd)) {
        fputs("OSD export rejected\n", stderr); goto done;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop;
    if (sigemptyset(&action.sa_mask) || sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGTERM, &action, NULL)) { perror("signals"); goto done; }
    fd = open("/dev/fb0", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &state) || !S_ISCHR(state.st_mode)) {
        fputs("framebuffer character device unavailable\n", stderr); goto done;
    }
    if (stopped) goto done;
    if (gkd_ui_plane_capabilities(fd, &caps)) { perror("plane capabilities"); goto done; }
    if (gkd_ui_plane_send(fd, tile, ttl, fade, 1U)) { perror("plane submit"); goto done; }
    printf("GKD_UI_PLANE_SUBMIT=accepted ttl_ms=%u fade_ms=%u source=explicit-test-data\n", ttl, fade);
    fflush(stdout);
    /* Hold only the test publisher's process lifetime. The kernel owns every
     * fade frame; this client never maps, draws into or polls the framebuffer. */
    waited = stopped ? -1 : poll(NULL, 0, (int)(ttl + 40U));
    if (waited < 0) {
        int wait_error = stopped ? 0 : errno;
        if (gkd_ui_plane_clear(fd)) { perror("plane clear"); goto done; }
        if (wait_error && wait_error != EINTR) {
            errno = wait_error; perror("publisher wait"); goto done;
        }
    }
    result = 0;
done:
    if (fd >= 0) (void)close(fd);
    free(tile);
    gkd_ui_font_release(&font);
    return result;
}
