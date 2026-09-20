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

static int number(const char *text, unsigned *value)
{
    unsigned parsed = 0U;
    if (!text || !*text) return -1;
    for (const char *p = text; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9') return -1;
        digit = (unsigned)(*p - '0');
        if (parsed > (GKD_UI_MENU_MAX_TTL_MS - digit) / 10U) return -1;
        parsed = parsed * 10U + digit;
    }
    if (parsed < GKD_UI_MENU_MIN_TTL_MS || parsed > GKD_UI_MENU_MAX_TTL_MS)
        return -1;
    *value = parsed;
    return 0;
}

int main(int argc, char **argv)
{
    static const struct gkd_ui_menu_item usb_items[] = {
        {"CHARGE", 0U}, {"STORAGE", 1U}, {"DEBUG", 2U},
    };
    static const struct gkd_ui_menu_item power_items[] = {
        {"SUSPEND", 2U}, {"REBOOT", 3U}, {"SHUTDOWN", 4U},
    };
    struct gkd_ui_config config;
    struct gkd_ui_font font;
    struct gkd_ui_surface surface;
    struct gkd_ui_menu menu;
    struct gkd_ui_menu_caps caps;
    struct sigaction action;
    struct stat state;
    uint16_t *pixels = NULL;
    unsigned ttl, transition;
    int fd = -1, result = 1, waited;

    if (argc != 5 || (strcmp(argv[2], "USB") && strcmp(argv[2], "POWER")) ||
        (strcmp(argv[4], "enabled") && strcmp(argv[4], "disabled")) ||
        number(argv[3], &ttl)) {
        fputs("usage: gkd-ui-menu-probe FONT USB|POWER TTL_MS enabled|disabled\n", stderr);
        return 2;
    }
    transition = !strcmp(argv[4], "enabled") ? 200U : 0U;
    gkd_ui_config_defaults(&config);
    if (gkd_ui_font_load(&font, argv[1])) { perror("font"); return 1; }
    pixels = calloc(GKD_UI_MENU_PIXELS, sizeof(*pixels));
    if (!pixels) { perror("pixels"); goto done; }
    surface = (struct gkd_ui_surface){pixels, GKD_UI_MENU_WIDTH,
        GKD_UI_MENU_HEIGHT, GKD_UI_MENU_WIDTH};
    menu = (struct gkd_ui_menu){!strcmp(argv[2], "USB") ? "USB MODE" : "POWER",
        !strcmp(argv[2], "USB") ? usb_items : power_items, 3U, 0U,
        GKD_UI_ACTION_DISABLED, GKD_UI_ACTION_DISABLED, "SELECT", "BACK"};
    if (gkd_ui_render_menu(&surface, &config, &font, &menu)) {
        fputs("menu render rejected\n", stderr); goto done;
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
    if (gkd_ui_menu_capabilities(fd, &caps)) { perror("menu capabilities"); goto done; }
    if (gkd_ui_menu_send(fd, pixels, ttl, transition, 1U)) { perror("menu submit"); goto done; }
    printf("GKD_UI_MENU_SUBMIT=accepted fixture=%s ttl_ms=%u transition_ms=%u display_only=1\n",
           argv[2], ttl, transition);
    fflush(stdout);
    waited = stopped ? -1 : poll(NULL, 0, (int)(ttl + 40U));
    if (waited < 0) {
        int wait_error = stopped ? 0 : errno;
        if (gkd_ui_menu_clear(fd)) { perror("menu clear"); goto done; }
        if (wait_error && wait_error != EINTR) {
            errno = wait_error; perror("publisher wait"); goto done;
        }
    }
    result = 0;
done:
    if (fd >= 0) (void)close(fd);
    free(pixels);
    gkd_ui_font_release(&font);
    return result;
}
