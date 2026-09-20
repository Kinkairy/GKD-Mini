/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define GKD_CONTROLS_CONFIG_ROOT "/tmp/gkd-controls-config-fixture"
#define socket fixture_socket
#define connect fixture_connect
#define send fixture_send
#define main gkd_controls_runner_main
#include "../source/gkd-controls.c"
#undef main
#undef send
#undef connect
#undef socket

static int save_calls, save_error, load_error, volume_set_calls, brightness_set_calls;
static struct gkd_hardware_state stored = {72U, 80U};
int gkd_controls_volume_read(struct gkd_controls_hardware *hw, int *value)
{ (void)hw; *value = 72; return 0; }
int gkd_controls_brightness_read(struct gkd_controls_hardware *hw, int *value)
{ (void)hw; *value = 80; return 0; }
int gkd_controls_volume_set(struct gkd_controls_hardware *hw, unsigned target, int *value)
{ (void)hw; ++volume_set_calls; *value = (int)target; return 0; }
int gkd_controls_brightness_set(struct gkd_controls_hardware *hw, unsigned target, int *value)
{ (void)hw; ++brightness_set_calls; *value = (int)target; return 0; }
int gkd_hardware_state_save(int dirfd, const struct gkd_hardware_state *state)
{ (void)dirfd; ++save_calls; if (save_error) { errno = EIO; return -1; } stored = *state; return 0; }
int gkd_hardware_state_load(int dirfd, struct gkd_hardware_state *state)
{ (void)dirfd; if (load_error) { errno = load_error; return -1; } *state = stored; return 0; }
void gkd_menu_guard_controls_init(struct gkd_menu_guard_controls *guard) { guard->lease_fd=-1; }
int gkd_menu_guard_controls_acquire(struct gkd_menu_guard_controls *guard, uint64_t *epoch)
{ (void)guard; *epoch=0; return 0; }
void gkd_menu_guard_controls_release(struct gkd_menu_guard_controls *guard) { (void)guard; }

static struct input_event queued[8];
static unsigned int queued_count, snapshot_mask, hardware_calls, export_calls, send_calls;
static unsigned int last_fade, last_ttl, last_sequence, yield_socket_calls, yield_connect_calls, yield_send_calls;
static unsigned int config_read_calls;
static int hardware_failure, send_failure, plane_busy, yield_connect_failure, yield_send_failure;
static char yield_packet[32];
static const char *resume_text;

int fixture_socket(int domain, int type, int protocol)
{
    ++yield_socket_calls;
    if (domain != AF_UNIX || type != (SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC) || protocol) {
        errno = EPROTO;
        return -1;
    }
    return open("/dev/null", O_RDONLY | O_CLOEXEC);
}
int fixture_connect(int fd, const struct sockaddr *address, socklen_t length)
{
    const struct sockaddr_un *unix_address = (const struct sockaddr_un *)address;
    ++yield_connect_calls;
    if (fd < 0 || length != sizeof(*unix_address) || unix_address->sun_family != AF_UNIX ||
        strcmp(unix_address->sun_path, "/run/gkd-application/control.sock")) {
        errno = EPROTO;
        return -1;
    }
    if (yield_connect_failure) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}
ssize_t fixture_send(int fd, const void *buffer, size_t length, int flags)
{
    ++yield_send_calls;
    if (fd < 0 || length >= sizeof(yield_packet) || flags != MSG_NOSIGNAL) {
        errno = EPROTO;
        return -1;
    }
    memcpy(yield_packet, buffer, length);
    yield_packet[length] = 0;
    if (yield_send_failure) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)length;
}

ssize_t __real_read(int fd, void *buffer, size_t length);

int gkd_controls_volume_adjust(struct gkd_controls_hardware *hardware,
	int delta, int *logical)
{
	(void)hardware; (void)delta;
	++hardware_calls;
	if (hardware_failure) return -1;
	*logical = 50;
	return 0;
}

int gkd_controls_brightness_cycle(struct gkd_controls_hardware *hardware,
	const unsigned *steps, unsigned count, int *percent)
{
	(void)hardware; (void)steps; (void)count;
	++hardware_calls;
	if (hardware_failure) return -1;
	*percent = 50;
	return 0;
}

int gkd_ui_export_osd_argb(uint32_t *pixels, size_t pixel_count,
	const struct gkd_ui_config *config, const struct gkd_ui_font *font,
	const struct gkd_ui_osd *osd)
{
	(void)pixel_count; (void)config; (void)font;
    pixels[0] = osd->icon; pixels[1] = (uint32_t)osd->level;
	++export_calls;
	return 0;
}

int gkd_ui_plane_send(int fd, const uint32_t *pixels, unsigned ttl_ms,
	unsigned fade_ms, unsigned sequence)
{
    (void)fd; (void)pixels;
    ++send_calls; last_ttl = ttl_ms; last_fade = fade_ms; last_sequence = sequence;
    if (plane_busy) { errno = EBUSY; return -1; }
    if (send_failure) { errno = EIO; return -1; }
	return 0;
}

ssize_t __wrap_read(int fd, void *buffer, size_t length)
{
	char descriptor[64], path[256];
	size_t bytes;
	(void)snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", fd);
	ssize_t target = readlink(descriptor, path, sizeof(path) - 1U);
	if (target > 0 && (size_t)target < sizeof(path)) {
		path[target] = 0;
		if (!strncmp(path, GKD_CONTROLS_CONFIG_ROOT "/generations/",
		             sizeof(GKD_CONTROLS_CONFIG_ROOT "/generations/") - 1U)) {
			++config_read_calls;
			return __real_read(fd, buffer, length);
		}
	}
	if (!queued_count) { errno = EAGAIN; return -1; }
	bytes = (size_t)queued_count * sizeof(queued[0]);
	if (bytes > length) { errno = EOVERFLOW; return -1; }
	memcpy(buffer, queued, bytes);
	queued_count = 0U;
	return (ssize_t)bytes;
}

ssize_t __wrap___read_chk(int fd, void *buffer, size_t length, size_t buffer_length)
{
	if (length > buffer_length) { errno = EOVERFLOW; return -1; }
	return __wrap_read(fd, buffer, length);
}

ssize_t __wrap_pread(int fd, void *buffer, size_t length, off_t offset)
{
    size_t n;
    (void)fd; (void)offset;
    if (!resume_text) { errno=EBADF; return -1; }
    n=strlen(resume_text); if (n>length) n=length;
    memcpy(buffer,resume_text,n); return (ssize_t)n;
}
ssize_t __wrap___pread_chk(int fd, void *buffer, size_t length, off_t offset,
    size_t buffer_length)
{
    if (length>buffer_length) { errno=EOVERFLOW; return -1; }
    return __wrap_pread(fd,buffer,length,offset);
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
	va_list arguments;
	unsigned char *bits;
	(void)fd;
	va_start(arguments, request);
	bits = va_arg(arguments, unsigned char *);
	va_end(arguments);
	if (request != EVIOCGKEY(KEY_BYTES)) { errno = EINVAL; return -1; }
	memset(bits, 0, KEY_BYTES);
	if (snapshot_mask & GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP))
		bits[KEY_KPPLUS / 8U] |= 1U << (KEY_KPPLUS % 8U);
	if (snapshot_mask & GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_DOWN))
		bits[KEY_KPMINUS / 8U] |= 1U << (KEY_KPMINUS % 8U);
	if (snapshot_mask & GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_BRIGHTNESS))
		bits[KEY_END / 8U] |= 1U << (KEY_END % 8U);
	return 0;
}

/* These only satisfy the renamed runner's unused startup/cleanup references. */
void gkd_input_owner_init(struct gkd_input_owner *owner) { memset(owner, 0, sizeof(*owner)); }
int gkd_input_observer_open(struct gkd_input_owner *owner) { (void)owner; return -1; }
void gkd_input_observer_close(struct gkd_input_owner *owner) { (void)owner; }
int gkd_controls_hardware_init(struct gkd_controls_hardware *hardware, int ctl, int brightness, int maximum)
{ (void)hardware; (void)ctl; (void)brightness; (void)maximum; return -1; }
void gkd_ui_config_defaults(struct gkd_ui_config *config) { memset(config, 0, sizeof(*config)); }
int gkd_ui_config_load(struct gkd_ui_config *config, const char *path, int sparse)
{ (void)config; (void)path; (void)sparse; return -1; }
int gkd_ui_font_load(struct gkd_ui_font *font, const char *path) { (void)font; (void)path; return -1; }
void gkd_ui_font_release(struct gkd_ui_font *font) { (void)font; }
int gkd_ui_plane_capabilities(int fd, struct gkd_ui_plane_caps *caps) { (void)fd; (void)caps; return -1; }
int gkd_ui_plane_clear(int fd) { (void)fd; return 0; }

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return -1; \
	} \
} while (0)

static int initialize(struct controller *controller, struct gkd_controls_state *state)
{
	struct gkd_controls_config config = {5U};
	memset(controller, 0, sizeof(*controller));
	controller->framebuffer = 7;
    controller->ttl = 300U;
    controller->fade_ms = 160U;
	controller->count = 2U;
	controller->steps[0] = 10U;
	controller->steps[1] = 100U;
	return gkd_controls_init(state, &config, effect, controller);
}

static int drain_paths(void)
{
	static const unsigned short keys[3] = {KEY_KPPLUS, KEY_KPMINUS, KEY_END};
	struct controller controller;
	struct gkd_controls_state state;
	int dropped = 0, result;
	CHECK(initialize(&controller, &state) == 0);
	queued[0] = (struct input_event){.type = EV_KEY, .code = KEY_KPPLUS, .value = 1};
	queued[1] = (struct input_event){.type = EV_KEY, .code = KEY_KPPLUS, .value = 0};
	queued_count = 2U; hardware_calls = 0U;
	CHECK(drain(&state, 3, 0U, keys, &dropped, 10U) == 0 && hardware_calls == 1U);
	CHECK(initialize(&controller, &state) == 0);
	snapshot_mask = GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP);
	queued[0] = (struct input_event){.type = EV_SYN, .code = SYN_DROPPED};
	queued[1] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
	queued[2] = (struct input_event){.type = EV_KEY, .code = KEY_KPPLUS, .value = 0};
	queued_count = 3U; dropped = 0; hardware_calls = 0U;
	result = drain(&state, 3, 0U, keys, &dropped, 20U);
	CHECK(result == 0 && !dropped && hardware_calls == 0U);
	CHECK(gkd_controls_set_blocked(&state, false, 21U) == 0);
	queued[0] = (struct input_event){.type = EV_KEY, .code = KEY_KPPLUS, .value = 1};
	queued_count = 1U;
	CHECK(drain(&state, 3, 0U, keys, &dropped, 21U) == 0 && hardware_calls == 1U);
	return 0;
}

static int effect_paths(void)
{
	struct controller controller;
	struct gkd_controls_state state;
	CHECK(initialize(&controller, &state) == 0);
	controller.osd_enabled = 1;
    hardware_calls = export_calls = send_calls = last_fade = last_ttl = last_sequence = 0U;
    hardware_failure = 1; send_failure = plane_busy = yield_connect_failure = yield_send_failure = 0;
	CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 5) < 0 &&
		hardware_calls == 1U && export_calls == 0U);
    hardware_failure = 0; send_failure = 1;
	CHECK(effect(&controller, GKD_CONTROLS_ACTION_BRIGHTNESS, 0) < 0 &&
		export_calls == 1U && !controller.published && !controller.pending);
    send_failure = 0;
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 2) == 0 &&
        send_calls == 2U && last_fade == 160U);
    controller.fade_ms = 0U;
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_BRIGHTNESS, 0) == 0 &&
        send_calls == 3U && last_fade == 0U);
	plane_busy = 1;
	CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 2) < 0 &&
		!controller.pending && !controller.yield_sent && yield_socket_calls == 0U);
	return 0;
}

static int install_generation(char digit, const char *text);
static void remove_generations(void);
static int pending_osd_contract(void)
{
    struct controller controller;
    struct gkd_controls_state state;
    uint64_t deadline;
    remove_generations();
    CHECK(initialize(&controller, &state) == 0);
    controller.osd_enabled = controller.managed = 1;
    CHECK(install_generation('1', "ui_dynamic_effects=enabled\n") == 0);
    hardware_calls = export_calls = send_calls = last_fade = last_ttl = last_sequence = 0U;
    yield_socket_calls = yield_connect_calls = yield_send_calls = 0U;
    hardware_failure = send_failure = yield_connect_failure = yield_send_failure = 0;
    plane_busy = 1;
    memset(yield_packet, 0, sizeof(yield_packet));
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 5) == 0 &&
        controller.pending && controller.yield_sent && !controller.published && controller.sequence == 0U &&
        hardware_calls == 1U && export_calls == 1U && send_calls == 1U &&
        yield_socket_calls == 1U && yield_connect_calls == 1U && yield_send_calls == 1U &&
        !strcmp(yield_packet, "osd-yield"));
    deadline = controller.pending_deadline;
    CHECK(pending_cycle(&controller, deadline - 100U, 0) == 0 && controller.pending &&
        hardware_calls == 1U && send_calls == 2U && controller.sequence == 0U &&
        yield_send_calls == 1U);
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_BRIGHTNESS, 0) == 0 &&
        controller.pending && hardware_calls == 2U && export_calls == 2U &&
        controller.tile[0] == 1U && controller.tile[1] == 50U);
    deadline = controller.pending_deadline;
    plane_busy = 0;
    CHECK(pending_cycle(&controller, deadline - 100U, 0) == 0 && !controller.pending &&
        controller.published && controller.sequence == 1U && last_sequence == 1U &&
        last_ttl == 100U && last_fade == 50U && hardware_calls == 2U);
    plane_busy = 1;
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 5) == 0 && controller.pending);
    CHECK(pending_cycle(&controller, controller.pending_deadline - 100U, 1) == 0 &&
        !controller.pending);
    CHECK(effect(&controller, GKD_CONTROLS_ACTION_VOLUME, 5) == 0 && controller.pending);
    deadline = controller.pending_deadline;
    CHECK(pending_cycle(&controller, deadline, 0) == 0 && !controller.pending);
    remove_generations();
    return 0;
}

static int resume_counter_contract(void)
{
    unsigned generation;
    resume_text="hide=2 suppressed=19 gate=1\n";
    CHECK(resume_generation(8,&generation)==0 && generation==2U);
    resume_text="hide=4294967295 suppressed=19 gate=0\n";
    CHECK(resume_generation(8,&generation)==0 && generation==UINT_MAX);
    resume_text="hide=1 suppressed=1 gate=2\n";
    CHECK(resume_generation(8,&generation)<0);
    resume_text="hide=1 suppressed=1 gate=0 trailing\n";
    CHECK(resume_generation(8,&generation)<0);
    resume_text=NULL;
    return 0;
}
static int menu_input_boundary(void)
{
    struct controller c; struct gkd_controls_state state;
    struct pollfd fds[2] = {{3, POLLIN, 0}, {4, POLLIN, 0}};
    unsigned short keys[2][3] = {{KEY_KPPLUS, KEY_KPMINUS, KEY_END},
                                 {KEY_KPPLUS, KEY_KPMINUS, KEY_END}};
    int dropped[2] = {0,0};
    CHECK(initialize(&c, &state) == 0); hardware_calls=0;
    queued[0] = (struct input_event){.type=EV_KEY,.code=KEY_KPPLUS,.value=1};
    queued_count=1;
    CHECK(input_cycle(&state, fds, keys, dropped, 10, 0, 0) == 0 && hardware_calls == 1);
    /* Menu grabbed release; quick close is visible only through changed epoch. */
    snapshot_mask=0; queued_count=0;
    CHECK(input_cycle(&state, fds, keys, dropped, 500, 1, 0) == 0 && hardware_calls == 1);
    CHECK(input_cycle(&state, fds, keys, dropped, 1000, 0, 0) == 0 && hardware_calls == 1);
    /* A held key at menu exit must release before a fresh press can act. */
    snapshot_mask=GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP);
    queued[0] = (struct input_event){.type=EV_KEY,.code=KEY_KPPLUS,.value=1}; queued_count=1;
    CHECK(input_cycle(&state, fds, keys, dropped, 1100, 1, 1) == 0 && hardware_calls == 1);
    CHECK(input_cycle(&state, fds, keys, dropped, 1200, 1, 0) == 0 && hardware_calls == 1);
    CHECK(input_cycle(&state, fds, keys, dropped, 2000, 0, 0) == 0 && hardware_calls == 1);
    snapshot_mask=0;
    CHECK(input_cycle(&state, fds, keys, dropped, 2100, 1, 0) == 0);
    queued[0] = (struct input_event){.type=EV_KEY,.code=KEY_KPPLUS,.value=1}; queued_count=1;
    CHECK(input_cycle(&state, fds, keys, dropped, 2200, 0, 0) == 0 && hardware_calls == 2);
    return 0;
}
static int persistence_contract(void)
{
    struct controller c; struct gkd_controls_state state;
    CHECK(initialize(&c, &state) == 0);
    c.saved = (struct gkd_hardware_state){50U,70U}; c.save_delay=1000U;
    CHECK(restore_state(&c) == 0 && volume_set_calls == 0);
    c.persistence=1;
    load_error=EPROTO;
    CHECK(restore_state(&c) < 0 && !c.state_ready && volume_set_calls == 0);
    load_error=ENOENT;
    CHECK(restore_state(&c) == 0 && c.saved.volume == 50U && c.saved.brightness == 70U);
    load_error=0;
    CHECK(restore_state(&c) == 0 && c.saved.volume == 72U && c.saved.brightness == 80U);
    CHECK(volume_set_calls == 2 && brightness_set_calls == 2);
    CHECK(effect(&c, GKD_CONTROLS_ACTION_VOLUME, 2) == 0 && c.dirty);
    CHECK(flush_state(&c, c.save_after-1U, 0) == 0 && save_calls == 0);
    CHECK(flush_state(&c, c.save_after, 0) == 0 && save_calls == 1 && !c.dirty);
    CHECK(effect(&c, GKD_CONTROLS_ACTION_BRIGHTNESS, 0) == 0 && c.dirty);
    CHECK(flush_state(&c, 0, 1) == 0 && save_calls == 2 && !c.dirty);
    CHECK(effect(&c, GKD_CONTROLS_ACTION_VOLUME, 2) == 0);
    save_error=1;
    CHECK(flush_state(&c, 0, 1) < 0 && c.dirty && c.persistence_failed && save_calls == 3);
    CHECK(flush_state(&c, 0, 1) == 0 && save_calls == 3);
    save_error=0;
    CHECK(initialize(&c, &state) == 0);
    c.persistence=1; c.state_ready=1; c.save_delay=1000U;
    CHECK(effect(&c, GKD_CONTROLS_ACTION_VOLUME, 2) == 0 && c.dirty);
    hardware_failure=1; /* Includes successful write with failed readback. */
    CHECK(effect(&c, GKD_CONTROLS_ACTION_VOLUME, 2) < 0 && c.persistence_failed);
    CHECK(flush_state(&c, 0, 1) == 0 && save_calls == 3);
    hardware_failure=0;
    return 0;
}
static int write_all(int fd, const char *text)
{
    size_t done=0, length=strlen(text);
    while(done<length){
        ssize_t n=write(fd,text+done,length-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;
        done+=(size_t)n;
    }
    return 0;
}
static int install_generation(char digit,const char *text)
{
    char hash[65],directory[256],file[320],target[96],temporary[256];
    int fd;
    memset(hash,digit,64U);hash[64]=0;
    if(mkdir(GKD_CONTROLS_CONFIG_ROOT,0700)&&errno!=EEXIST)return -1;
    if(mkdir(GKD_CONTROLS_CONFIG_ROOT "/generations",0700)&&errno!=EEXIST)return -1;
    if(snprintf(directory,sizeof(directory),GKD_CONTROLS_CONFIG_ROOT "/generations/%s",hash)>=(int)sizeof(directory) ||
       snprintf(file,sizeof(file),"%s/effective.conf",directory)>=(int)sizeof(file) ||
       snprintf(target,sizeof(target),"generations/%s",hash)>=(int)sizeof(target) ||
       snprintf(temporary,sizeof(temporary),GKD_CONTROLS_CONFIG_ROOT "/.current-%c",digit)>=(int)sizeof(temporary))
        return -1;
    if(mkdir(directory,0700))return -1;
    fd=open(file,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0444);
    if(fd<0)return -1;
    if(write_all(fd,text)||fsync(fd)||close(fd)||chmod(file,0444)||chmod(directory,0555))return -1;
    (void)unlink(temporary);
    if(symlink(target,temporary)||rename(temporary,GKD_CONTROLS_CONFIG_ROOT "/current"))return -1;
    return 0;
}
static void remove_generations(void)
{
    char hash[65],directory[256],file[320];
    (void)unlink(GKD_CONTROLS_CONFIG_ROOT "/current");
    for(char digit='1';digit<='5';++digit){
        memset(hash,digit,64U);hash[64]=0;
        (void)snprintf(directory,sizeof(directory),GKD_CONTROLS_CONFIG_ROOT "/generations/%s",hash);
        (void)snprintf(file,sizeof(file),"%s/effective.conf",directory);
        (void)chmod(directory,0700);(void)unlink(file);(void)rmdir(directory);
    }
    (void)rmdir(GKD_CONTROLS_CONFIG_ROOT "/generations");
    (void)rmdir(GKD_CONTROLS_CONFIG_ROOT);
}
static int dynamic_effects_contract(void)
{
    struct controller c;struct gkd_controls_state state;
    struct pollfd fds[2]={{3,POLLIN,0},{4,POLLIN,0}};
    unsigned short keys[2][3]={{KEY_KPPLUS,KEY_KPMINUS,KEY_END},{KEY_KPPLUS,KEY_KPMINUS,KEY_END}};
    int dropped[2]={0,0};unsigned calls,reads;
    plane_busy=0;
    remove_generations();
    CHECK(initialize(&c,&state)==0);c.managed=1;c.osd_enabled=1;
    CHECK(install_generation('1',"volume_step=2\nui_dynamic_effects=enabled\n")==0);
    queued_count=0;snapshot_mask=0;
    CHECK(input_cycle(&state,fds,keys,dropped,10,0,0)==0&&!c.effects_valid);
    hardware_calls=send_calls=0;
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)==0&&last_fade==160U&&c.effects_valid);
    reads=config_read_calls;
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)==0&&last_fade==160U&&config_read_calls==reads);
    calls=hardware_calls;
    CHECK(install_generation('2',"volume_step=2\nui_dynamic_effects=disabled\n")==0);
    CHECK(effect(&c,GKD_CONTROLS_ACTION_BRIGHTNESS,0)==0&&last_fade==0U&&hardware_calls==calls+1U);
    CHECK(install_generation('3',"ui_dynamic_effects=enabled\nvolume_step=2\n")==0);
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)==0&&last_fade==160U);
    calls=hardware_calls;
    CHECK(install_generation('4',"volume_step=2\nui_dynamic_effects=bogus\n")==0);
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)<0&&hardware_calls==calls);
    CHECK(install_generation('5',"volume_step=2\n")==0);
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)<0&&hardware_calls==calls);
    CHECK(unlink(GKD_CONTROLS_CONFIG_ROOT "/current")==0);
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)<0&&hardware_calls==calls);
    /* Unmanaged/R keeps its launch-time value and never consults A's snapshot. */
    CHECK(initialize(&c,&state)==0);c.osd_enabled=1;c.fade_ms=0;
    CHECK(effect(&c,GKD_CONTROLS_ACTION_VOLUME,2)==0&&last_fade==0U);
    remove_generations();
    return 0;
}
int main(void)
{
	if (drain_paths() || effect_paths() || pending_osd_contract() || resume_counter_contract() || menu_input_boundary() ||
	    persistence_contract() || dynamic_effects_contract()) return 1;
	puts("GKD_CONTROLS_RUNNER=PASS dynamic-effects=next-entry cache=inode idle-polls=0 invalid=reject unmanaged=preserved");
	return 0;
}
