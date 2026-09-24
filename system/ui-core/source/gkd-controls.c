/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-controls-state.h"
#include "gkd-controls-hardware.h"
#include "gkd-input-owner.h"
#include "gkd-input-keys.h"
#include "gkd-hardware-state.h"
#include "gkd-menu-guard.h"
#include "gkd-ui.h"
#include "gkd-ui-plane-client.h"
#include "gkd-controls-command.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define KEY_BYTES ((KEY_MAX + 8U) / 8U)
#ifndef GKD_CONTROLS_CONFIG_ROOT
#define GKD_CONTROLS_CONFIG_ROOT "/run/gkd-config"
#endif
#define GKD_CONTROLS_GENERATION_PREFIX "generations/"
#define GKD_CONTROLS_GENERATION_HEX 64U
#define GKD_CONTROLS_CONFIG_MAX 32768U
#define GKD_CONTROLS_SERVICE_SOCKET "/run/gkd-application/control.sock"
#define GKD_CONTROLS_OSD_YIELD "osd-yield"
struct controller {
    struct gkd_controls_hardware hardware;
    struct gkd_ui_config ui;
    struct gkd_ui_font font;
    uint32_t tile[GKD_UI_PLANE_PIXELS];
    unsigned steps[16], count, ttl, fade_ms, sequence;
    int framebuffer, osd_enabled, published, managed;
    int pending, yield_sent;
    uint64_t pending_deadline;
    unsigned pending_fade;
    dev_t effects_dev;
    ino_t effects_ino;
    unsigned effects_fade;
    int effects_valid;
    int state_fd, persistence, state_ready, dirty, persistence_failed;
    unsigned save_delay;
    uint64_t save_after;
    struct gkd_hardware_state saved;

};
static volatile sig_atomic_t stopped;
static void stop(int signo) { (void)signo; stopped = 1; }
static int reject(int error) { errno = error; return -1; }
static int number(const char *s, unsigned maximum, unsigned *out)
{
    unsigned n = 0;
    if (!s || !*s) return -1;
    for (; *s; ++s) {
        unsigned d;
        if (*s < '0' || *s > '9') return -1;
        d = (unsigned)(*s - '0');
        if (d > maximum || n > (maximum - d) / 10U) return -1;
        n = n * 10U + d;
    }
    *out = n; return 0;
}
static int steps_parse(struct controller *c, const char *s)
{
    char copy[80], *part, *save = NULL;
    size_t len = strlen(s);
    if (!len || len >= sizeof(copy) || s[0] == ',' || s[len-1U] == ',' || strstr(s, ",,")) return -1;
    memcpy(copy, s, len + 1U);
    for (part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
        unsigned n;
        if (c->count >= 16U || number(part, 100U, &n) || !n ||
            (c->count && n <= c->steps[c->count-1U])) return -1;
        c->steps[c->count++] = n;
    }
    return c->count >= 2U ? 0 : -1;
}
static int now_ms(uint64_t *now)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    if (t.tv_sec < 0 || (uint64_t)t.tv_sec > (UINT64_MAX-999U)/1000U ||
        t.tv_nsec < 0 || t.tv_nsec >= 1000000000L) return reject(EOVERFLOW);
    *now = (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
    return 0;
}
static int open_checked(const char *path, int flags, int character)
{
    struct stat st;
    int fd = open(path, flags | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || (character ? !S_ISCHR(st.st_mode) : !S_ISREG(st.st_mode))) {
        (void)close(fd); return reject(ENODEV);
    }
    return fd;
}
static int generation_name(const char *name, size_t length)
{
    static const char prefix[] = GKD_CONTROLS_GENERATION_PREFIX;
    size_t i;
    if (length != sizeof(prefix) - 1U + GKD_CONTROLS_GENERATION_HEX ||
        memcmp(name, prefix, sizeof(prefix) - 1U)) return reject(EPROTO);
    for (i = sizeof(prefix) - 1U; i < length; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') ||
              (name[i] >= 'a' && name[i] <= 'f'))) return reject(EPROTO);
    return 0;
}
static int parse_dynamic_effects(char *text, size_t length, unsigned *fade)
{
    static const char key[] = "ui_dynamic_effects=";
    char *line = text, *end = text + length;
    int found = 0;
    if (!length || text[length - 1U] != '\n') return reject(EPROTO);
    while (line < end) {
        char *newline = memchr(line, '\n', (size_t)(end - line));
        char *equals;
        if (!newline || newline == line ||
            memchr(line, 0, (size_t)(newline - line)) ||
            memchr(line, '\r', (size_t)(newline - line))) return reject(EPROTO);
        *newline = 0;
        equals = strchr(line, '=');
        if (!equals || equals == line || equals + 1 == newline) return reject(EPROTO);
        for (char *p = line; p < equals; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
                return reject(EPROTO);
        if (!strncmp(line, key, sizeof(key) - 1U)) {
            const char *value = line + sizeof(key) - 1U;
            if (found++) return reject(EPROTO);
            if (!strcmp(value, "enabled")) *fade = 160U;
            else if (!strcmp(value, "disabled")) *fade = 0U;
            else return reject(EPROTO);
        }
        line = newline + 1;
    }
    return found == 1 ? 0 : reject(EPROTO);
}
static int current_effects(struct controller *c, unsigned *fade)
{
    char generation_name_text[sizeof(GKD_CONTROLS_GENERATION_PREFIX) +
        GKD_CONTROLS_GENERATION_HEX];
    char text[GKD_CONTROLS_CONFIG_MAX + 1U];
    struct stat root_stat, generation_stat, file_stat;
    ssize_t length, count, extra;
    size_t done = 0;
    unsigned candidate = 0;
    int root = -1, generation = -1, file = -1, result = -1, saved;

    if (!c || !fade) return reject(EINVAL);
    root = open(GKD_CONTROLS_CONFIG_ROOT, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0 || fstat(root, &root_stat)) goto done;
    if (!S_ISDIR(root_stat.st_mode) || root_stat.st_uid != geteuid() ||
        (root_stat.st_mode & 0022U)) { errno = EPERM; goto done; }
    length = readlinkat(root, "current", generation_name_text,
                        sizeof(generation_name_text));
    if (length < 0 || (size_t)length >= sizeof(generation_name_text) ||
        generation_name(generation_name_text, (size_t)length)) goto done;
    generation_name_text[length] = 0;
    generation = openat(root, generation_name_text,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (generation < 0 || fstat(generation, &generation_stat)) goto done;
    if (!S_ISDIR(generation_stat.st_mode) || generation_stat.st_uid != geteuid() ||
        (generation_stat.st_mode & 0222U)) { errno = EPERM; goto done; }
    file = openat(generation, "effective.conf", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0 || fstat(file, &file_stat)) goto done;
    if (!S_ISREG(file_stat.st_mode) || file_stat.st_uid != geteuid() ||
        file_stat.st_nlink != 1 || (file_stat.st_mode & 0222U) ||
        file_stat.st_size <= 0 || file_stat.st_size > (off_t)GKD_CONTROLS_CONFIG_MAX) {
        errno = EPROTO; goto done;
    }
    if (c->effects_valid && c->effects_dev == file_stat.st_dev &&
        c->effects_ino == file_stat.st_ino) {
        *fade = c->effects_fade;
        result = 0;
        goto done;
    }
    while (done < (size_t)file_stat.st_size) {
        count = read(file, text + done, (size_t)file_stat.st_size - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; goto done; }
        done += (size_t)count;
    }
    do extra = read(file, text, 1U); while (extra < 0 && errno == EINTR);
    if (extra != 0 || parse_dynamic_effects(text, done, &candidate)) {
        if (extra > 0) errno = EOVERFLOW;
        goto done;
    }
    c->effects_dev = file_stat.st_dev;
    c->effects_ino = file_stat.st_ino;
    c->effects_fade = candidate;
    c->effects_valid = 1;
    *fade = candidate;
    result = 0;
done:
    saved = errno;
    if (file >= 0) (void)close(file);
    if (generation >= 0) (void)close(generation);
    if (root >= 0) (void)close(root);
    errno = saved;
    return result;
}
static int lock_controller(void)
{
    struct stat st;
    int fd = open("/run/gkd-controls.lock", O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_gid ||
        st.st_nlink != 1 || (st.st_mode & 07777U) != 0600U || flock(fd, LOCK_EX|LOCK_NB)) {
        (void)close(fd); return reject(EBUSY);
    }
    return fd; /* Keep the stable inode; never unlink a lock another process may open. */
}
static int capture_state(struct controller *c)
{
    int volume, brightness; uint64_t now;
    if (!c->persistence || !c->state_ready) return 0;
    if (gkd_controls_volume_read(&c->hardware, &volume) ||
        gkd_controls_brightness_read(&c->hardware, &brightness) || now_ms(&now)) return -1;
    if (volume < 0 || volume > 100 || brightness < 1 || brightness > 100 ||
        now > UINT64_MAX - c->save_delay) return reject(ERANGE);
    c->saved = (struct gkd_hardware_state){(unsigned)volume, (unsigned)brightness};
    c->dirty = 1; c->save_after = now + c->save_delay;
    return 0;
}
static int flush_state(struct controller *c, uint64_t now, int final)
{
    if (!c->dirty || c->persistence_failed || (!final && now < c->save_after)) return 0;
    if (gkd_hardware_state_save(c->state_fd, &c->saved)) {
        c->persistence_failed = 1;
        fputs("GKD_CONTROLS state save failed; durability not confirmed\n", stderr);
        return -1;
    }
    c->dirty = 0; return 0;
}
static int restore_state(struct controller *c)
{
    int value;
    if (!c->persistence) return 0;
    if (gkd_hardware_state_load(c->state_fd, &c->saved) && errno != ENOENT) return -1;
    if (gkd_controls_volume_set(&c->hardware, c->saved.volume, &value)) return -1;
    if (gkd_controls_brightness_set(&c->hardware, c->saved.brightness, &value)) {
        fputs("GKD_CONTROLS startup volume applied; brightness restore failed\n", stderr);
        return -1;
    }
    c->state_ready = 1;
    return 0;
}
static void pending_clear(struct controller *c)
{
    c->pending = 0;
    c->yield_sent = 0;
    c->pending_deadline = 0;
    c->pending_fade = 0;
}
static int request_osd_yield(void)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int fd, result = -1, saved;
    size_t length = sizeof(GKD_CONTROLS_OSD_YIELD) - 1U;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
                 GKD_CONTROLS_SERVICE_SOCKET) >= (int)sizeof(address.sun_path))
        return reject(ENAMETOOLONG);
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (!connect(fd, (struct sockaddr *)&address, sizeof(address)) &&
        send(fd, GKD_CONTROLS_OSD_YIELD, length, MSG_NOSIGNAL) == (ssize_t)length)
        result = 0;
    saved = errno;
    (void)close(fd);
    if (result) errno = saved;
    return result;
}
static int publish_tile(struct controller *c, unsigned ttl, unsigned fade)
{
    unsigned sequence;
    if (c->sequence == UINT_MAX) return reject(EOVERFLOW);
    sequence = c->sequence + 1U;
    if (gkd_ui_plane_send(c->framebuffer, c->tile, ttl, fade, sequence)) return -1;
    c->sequence = sequence;
    c->published = 1;
    return 0;
}
static int pending_cycle(struct controller *c, uint64_t now, int barrier)
{
    unsigned remaining, fade;
    if (barrier) {
        pending_clear(c);
        return 0;
    }
    if (!c->pending) return 0;
    if (now >= c->pending_deadline) {
        pending_clear(c);
        return 0;
    }
    remaining = (unsigned)(c->pending_deadline - now);
    if (remaining < GKD_UI_PLANE_MIN_TTL_MS) {
        pending_clear(c);
        return 0;
    }
    fade = c->pending_fade;
    if (fade > remaining / 2U) fade = remaining / 2U;
    if (!publish_tile(c, remaining, fade)) {
        pending_clear(c);
        return 0;
    }
    if (errno == EBUSY && c->managed) {
        if (!c->yield_sent && !request_osd_yield()) c->yield_sent = 1;
        return 0;
    }
    return -1;
}
static int effect(void *context, enum gkd_controls_action action, int delta)
{
    struct controller *c = context;
    struct gkd_ui_osd osd;
    char text[14]; int value; uint64_t now;
    unsigned fade;
    int result;
    if (stopped) return reject(EINTR);
    fade = c->fade_ms;
    if (c->managed && c->osd_enabled && current_effects(c, &fade)) {
        fputs("GKD_CONTROLS effective config invalid; action rejected\n", stderr);
        return -1;
    }
    result = action == GKD_CONTROLS_ACTION_VOLUME ?
        gkd_controls_volume_adjust(&c->hardware, delta, &value) :
        gkd_controls_brightness_cycle(&c->hardware, c->steps, c->count, &value);
    if (result) {
        /* A write may already have succeeded. Never save an older cached value
         * after readback failed; the actual hardware state is now uncertain. */
        c->persistence_failed = 1;
        perror("GKD_CONTROLS hardware write/readback failed"); return -1;
    }
    if (capture_state(c)) {
        c->persistence_failed = 1;
        fputs("GKD_CONTROLS hardware applied; state capture failed\n", stderr); return -1;
    }
    if (!c->osd_enabled) return 0;
    if (now_ms(&now) || now > UINT64_MAX - c->ttl) {
        fputs("GKD_CONTROLS hardware applied; OSD failed; controller stopping\n", stderr);
        return -1;
    }
    (void)snprintf(text, sizeof(text), "%d%%", value);
    osd = (struct gkd_ui_osd){text, action == GKD_CONTROLS_ACTION_VOLUME ? 0U : 1U, value, 0};
    if (c->sequence == UINT_MAX || gkd_ui_export_osd_argb(c->tile,
        GKD_UI_PLANE_PIXELS, &c->ui, &c->font, &osd)) {
        fputs("GKD_CONTROLS hardware applied; OSD failed; controller stopping\n", stderr);
        return -1;
    }
    if (!publish_tile(c, c->ttl, fade)) {
        pending_clear(c);
        return 0;
    }
    if (errno == EBUSY && c->managed) {
        c->pending = 1;
        c->pending_deadline = now + c->ttl;
        c->pending_fade = fade;
        if (!c->yield_sent && !request_osd_yield()) c->yield_sent = 1;
        return 0;
    }
    fputs("GKD_CONTROLS hardware applied; OSD failed; controller stopping\n", stderr);
    return -1;
}
static int resume_generation(int fd, unsigned *generation)
{
    char text[80]; unsigned hide, suppressed, gate; int used = 0;
    ssize_t n = pread(fd, text, sizeof(text)-1U, 0);
    if (n < 0) return -1;
    if (!n || n >= (ssize_t)sizeof(text)-1) return reject(EPROTO);
    text[n] = 0;
    if (sscanf(text, "hide=%u suppressed=%u gate=%u%n", &hide, &suppressed, &gate, &used) != 3 ||
        gate > 1U ||
        (strcmp(text+used, "\n") && text[used])) return reject(EPROTO);
    /* hide/suppressed are cumulative diagnostics, not held blocking states.
     * gate only prevents an obsolete Loading writer from renewing after resume. */
    *generation = hide; return 0;
}
static int key_snapshot(int fd, const unsigned short keys[3], uint32_t *mask, int validate)
{
    unsigned char bits[KEY_BYTES]; unsigned i;
    memset(bits, 0, sizeof(bits));
    if (validate) {
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return -1;
        for (i=0; i<3U; ++i) if (!(bits[keys[i]/8U] & (1U << (keys[i]%8U)))) return reject(ENODEV);
        memset(bits, 0, sizeof(bits));
    }
    if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return -1;
    *mask = 0;
    for (i=0; i<3U; ++i) if (bits[keys[i]/8U] & (1U << (keys[i]%8U))) *mask |= 1U << i;
    return 0;
}
/* Drain releases before ticking. Lost events suppress held keys until release. */
static int drain(struct gkd_controls_state *state, int fd, unsigned source,
                 const unsigned short keys[3], int *dropped, uint64_t now)
{
    struct input_event events[32]; unsigned batches = 0;
    for (;;) {
        ssize_t n = read(fd, events, sizeof(events));
        if (n < 0 && errno == EAGAIN) return 0;
        if (n < 0 && errno == EINTR) { if (stopped) return 0; continue; }
        if (n <= 0 || n % (ssize_t)sizeof(events[0])) return reject(EIO);
        for (size_t i=0; i<(size_t)n/sizeof(events[0]); ++i) {
            const struct input_event *event = &events[i];
            if (event->type == EV_SYN && event->code == SYN_DROPPED) {
                *dropped = 1;
                if (gkd_controls_set_blocked(state, true, now)) return -1;
                if (gkd_controls_resync(state, (enum gkd_controls_source)source,
                                       state->held[source], now)) return -1;
            } else if (*dropped) {
                if (event->type == EV_SYN && event->code == SYN_REPORT) {
                    uint32_t mask;
                    if (key_snapshot(fd, keys, &mask, 0) ||
                        gkd_controls_resync(state, (enum gkd_controls_source)source, mask, now)) return -1;
                    *dropped = 0;
                }
            } else if (event->type == EV_KEY) {
                for (unsigned k=0; k<3U; ++k) if (event->code == keys[k] &&
                    gkd_controls_event(state, (enum gkd_controls_source)source,
                                       (enum gkd_controls_key)k, event->value, now)) return -1;
            }
        }
        /* A flooded device must not starve stop/gate handling indefinitely. */
        if (++batches >= 64U) return reject(EOVERFLOW);
    }
}
/* Called with a shared menu lease when effects are allowed. This exact input
 * boundary is exercised by the runner fixture for missed menu release events. */
static int input_cycle(struct gkd_controls_state *state, struct pollfd pollers[2],
    unsigned short keys[2][3], int dropped[2], uint64_t now, int barrier, int menu_busy)
{
    if (gkd_controls_set_blocked(state, barrier || dropped[0] || dropped[1], now)) return -1;
    for (unsigned s=0; s<2U; ++s) {
        if (pollers[s].revents & (POLLERR|POLLHUP|POLLNVAL)) { errno=ENODEV; return -1; }
        if (drain(state, pollers[s].fd, s, keys[s], &dropped[s], now)) return -1;
    }
    if (barrier) {
        /* Drain pre-resume events without effects, then establish real held keys. */
        for (unsigned s=0; s<2U; ++s) {
            uint32_t mask;
            if (key_snapshot(pollers[s].fd, keys[s], &mask, 0) ||
                gkd_controls_resync(state, (enum gkd_controls_source)s, mask, now)) return -1;
        }
    }
    if (stopped) return 0;
    if (gkd_controls_set_blocked(state, menu_busy || dropped[0] || dropped[1], now) ||
        gkd_controls_tick(state, now)) return -1;
    return 0;
}
static int command_open(void)
{
    struct sockaddr_un address={.sun_family=AF_UNIX};struct stat st;
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    mode_t previous;
    if(fd<0)return -1;
    if(lstat(GKD_CONTROLS_COMMAND_SOCKET,&st)==0){
        if(!S_ISSOCK(st.st_mode)||st.st_uid||unlink(GKD_CONTROLS_COMMAND_SOCKET))goto fail;
    }else if(errno!=ENOENT)goto fail;
    strcpy(address.sun_path,GKD_CONTROLS_COMMAND_SOCKET);
    previous=umask(0177);
    int bound=bind(fd,(struct sockaddr *)&address,sizeof(address));
    umask(previous);
    if(bound)goto fail;
    return fd;
fail:{int saved=errno;close(fd);errno=saved;return -1;}
}
static int command_drain(struct controller *c,int fd)
{
    for(unsigned batch=0;batch<8U;batch++){
        char command=0;
        ssize_t got=recv(fd,&command,1,MSG_DONTWAIT|MSG_TRUNC);
        if(got<0)return errno==EAGAIN||errno==EWOULDBLOCK?0:-1;
        if(got!=1||command!=GKD_CONTROLS_COMMAND_BRIGHTNESS)continue;
        if(effect(c,GKD_CONTROLS_ACTION_BRIGHTNESS,0))return -1;
    }
    return 0;
}
int main(int argc, char **argv)
{
    struct controller c = {0};
    struct gkd_controls_config config;
    struct gkd_controls_state state;
    struct gkd_input_owner input;
    struct gkd_menu_guard_controls menu;
    struct gkd_ui_plane_caps caps;
    struct sigaction action;
    struct pollfd pollers[3];
    unsigned short keys[2][3] = {{KEY_KPPLUS, KEY_KPMINUS, KEY_END}, {0,0,0}};
    int ctl=-1, brightness=-1, maximum=-1, gate=-1, lock=-1,command=-1;
    int result=1, dropped[2]={0,0}, managed, ready_fd=-1, menu_busy=0;
    uint64_t menu_epoch=0, previous_epoch=0;

    unsigned generation, previous_generation;
    uint64_t now;
    c.framebuffer = -1; c.state_fd = -1;
    gkd_menu_guard_controls_init(&menu);
    gkd_input_owner_init(&input);
    managed = argc == 16 && !strcmp(argv[9], "--managed");
    c.managed = managed;
    if ((!managed && argc != 9) || number(argv[1], 100U, &config.volume_step) || !config.volume_step ||
        steps_parse(&c, argv[2]) || number(argv[3], 5000U, &c.ttl) || c.ttl < 200U ||
        (strcmp(argv[4], "enabled") && strcmp(argv[4], "disabled")) ||
        (strcmp(argv[5], "enabled") && strcmp(argv[5], "disabled"))) goto usage;
    c.osd_enabled = !strcmp(argv[4], "enabled");
    c.fade_ms = !strcmp(argv[5], "enabled") ? 160U : 0U;
    for (unsigned k=0; k<3U; ++k) if (gkd_input_key_code(argv[6U+k], &keys[1][k])) goto usage;
    if (keys[1][0] == keys[1][1] || keys[1][0] == keys[1][2] || keys[1][1] == keys[1][2]) goto usage;
    if (managed) {
        unsigned state_number, ready_number;
        struct stat st; int flags;
        if (number(argv[10], INT_MAX, &state_number) || state_number < 3U ||
            number(argv[11], INT_MAX, &ready_number) || ready_number < 3U ||
            state_number == ready_number ||
            (strcmp(argv[12], "enabled") && strcmp(argv[12], "disabled")) ||
            number(argv[13], 5000U, &c.save_delay) || c.save_delay < 200U ||
            number(argv[14], 100U, &c.saved.volume) ||
            number(argv[15], 100U, &c.saved.brightness) || !c.saved.brightness) goto usage;
        c.state_fd = (int)state_number; ready_fd = (int)ready_number;
        c.persistence = !strcmp(argv[12], "enabled");
        if (fstat(c.state_fd, &st) || !S_ISDIR(st.st_mode) || st.st_uid ||
            (st.st_mode & 0022U) || fcntl(c.state_fd, F_SETFD, FD_CLOEXEC) ||
            fstat(ready_fd, &st) || !S_ISFIFO(st.st_mode) ||
            (flags = fcntl(ready_fd, F_GETFL)) < 0 || (flags & O_ACCMODE) != O_WRONLY ||
            fcntl(ready_fd, F_SETFL, flags | O_NONBLOCK) ||
            fcntl(ready_fd, F_SETFD, FD_CLOEXEC)) goto done;
    }
    if (geteuid()) { errno = EPERM; goto done; }
    memset(&action, 0, sizeof(action)); action.sa_handler = stop;
    if (sigemptyset(&action.sa_mask) || sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGTERM, &action, NULL)) goto done;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) goto done;
    lock = lock_controller(); if (lock < 0) goto done;
    ctl = open_checked("/dev/snd/controlC0", O_RDWR, 1);
    brightness = open_checked("/sys/class/backlight/gkd350-backlight/brightness", O_RDWR, 0);
    maximum = open_checked("/sys/class/backlight/gkd350-backlight/max_brightness", O_RDONLY, 0);
    gate = open_checked("/sys/class/graphics/fb0/gkd_resume_state", O_RDONLY, 0);
    if (ctl < 0 || brightness < 0 || maximum < 0 || gate < 0 ||
        gkd_controls_hardware_init(&c.hardware, ctl, brightness, maximum)) goto done;
    if (c.osd_enabled) {
        gkd_ui_config_defaults(&c.ui);
        if (gkd_ui_config_load(&c.ui, "/etc/gkd-mini/gdkmini.ui.conf", 0) ||
            gkd_ui_font_load(&c.font, c.ui.font_path)) goto done;
        c.framebuffer = open_checked("/dev/fb0", O_RDWR, 1);
        if (c.framebuffer < 0 || gkd_ui_plane_capabilities(c.framebuffer, &caps)) goto done;
        for (unsigned icon=0; icon<2U; ++icon) {
            struct gkd_ui_osd preflight = {"100%", icon, 100, 0};
            if (gkd_ui_export_osd_argb(c.tile, GKD_UI_PLANE_PIXELS,
                &c.ui, &c.font, &preflight)) goto done;
        }
    }
    if ((command=command_open())<0||gkd_input_observer_open(&input) || now_ms(&now) ||
        gkd_controls_init(&state, &config, effect, &c)) goto done;
    pollers[0] = (struct pollfd){input.physical_fd, POLLIN, 0};
    pollers[1] = (struct pollfd){input.virtual_fd, POLLIN, 0};
    pollers[2] = (struct pollfd){command, POLLIN, 0};
    if (resume_generation(gate, &previous_generation) ||
        gkd_controls_set_blocked(&state, true, now)) goto done;
    for (unsigned s=0; s<2U; ++s) {
        uint32_t mask;
        if (key_snapshot(pollers[s].fd, keys[s], &mask, 1) ||
            drain(&state, pollers[s].fd, s, keys[s], &dropped[s], now) ||
            key_snapshot(pollers[s].fd, keys[s], &mask, 0) ||
            gkd_controls_resync(&state, (enum gkd_controls_source)s, mask, now)) goto done;
    }
    {
        int acquired = gkd_menu_guard_controls_acquire(&menu, &previous_epoch);
        if (acquired < 0) goto done;
        menu_busy = acquired == 1;
        if (managed && c.persistence && (menu_busy || restore_state(&c))) goto done;
        gkd_menu_guard_controls_release(&menu);
    }
    if (stopped) { errno = EINTR; goto done; }
    if (managed) {
        if (write(ready_fd, "R", 1) != 1) goto done;
        (void)close(ready_fd); ready_fd = -1;
    }
    fputs(c.persistence ? "GKD_CONTROLS ready; persisted/default hardware restored\n" :
          "GKD_CONTROLS ready; startup hardware preserved\n", stderr);
    while (!stopped) {
        int wait_ms=100, ready, resumed, acquired, barrier; int64_t timeout;
        if (now_ms(&now)) goto done;
        timeout = gkd_controls_timeout_ms(&state, now);
        if (timeout >= 0 && timeout < wait_ms) wait_ms = (int)timeout;
        ready = poll(pollers, 3, wait_ms);
        if (ready < 0) { if (errno == EINTR) continue; goto done; }
        if (stopped) break;
        if (now_ms(&now) || resume_generation(gate, &generation)) goto done;
        acquired = gkd_menu_guard_controls_acquire(&menu, &menu_epoch);
        if (acquired < 0) goto done;
        barrier = acquired == 1 || menu_busy || menu_epoch != previous_epoch;
        menu_busy = acquired == 1;
        if (!menu_busy) previous_epoch = menu_epoch;
        resumed = generation != previous_generation;
        previous_generation = generation;
        if (resumed || barrier) pending_clear(&c);
        if (input_cycle(&state, pollers, keys, dropped, now, resumed || barrier, menu_busy)) goto done;
        if(pollers[2].revents&(POLLERR|POLLHUP|POLLNVAL))goto done;
        if((pollers[2].revents&POLLIN)&&command_drain(&c,command))goto done;
        if (!resumed && !barrier && pending_cycle(&c, now, 0)) goto done;
        gkd_menu_guard_controls_release(&menu);
        if (flush_state(&c, now, 0)) goto done;
    }
    result=0;
done:
    gkd_menu_guard_controls_release(&menu);
    if (flush_state(&c, 0, 1)) result = 1;
    if (result) perror("GKD_CONTROLS stopped");
    if (c.published && gkd_ui_plane_clear(c.framebuffer)) {
        perror("GKD_CONTROLS clear failed; kernel TTL remains authoritative"); result=1;
    }
    gkd_input_observer_close(&input);
    gkd_ui_font_release(&c.font);
    { int fds[] = {ctl,brightness,maximum,gate,c.framebuffer,lock,c.state_fd,ready_fd,command};
      for (unsigned i=0; i<sizeof(fds)/sizeof(fds[0]); ++i) if (fds[i]>=0) (void)close(fds[i]); }
    if(command>=0)(void)unlink(GKD_CONTROLS_COMMAND_SOCKET);
    return result;
usage:
    fputs("usage: gkd-controls STEP BRIGHTNESS_STEPS TTL_MS OSD_ENABLED|disabled EFFECTS_ENABLED|disabled VOLUME_UP_KEY VOLUME_DOWN_KEY BRIGHTNESS_KEY [--managed STATE_FD READY_FD PERSIST SAVE_DELAY DEFAULT_VOLUME DEFAULT_BRIGHTNESS]\n", stderr);
    return 2;
}
