// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "gkd-ui.h"
#if GKD_DEDICATED_RECOVERY
#include "gkd-input-owner.h"
int gkd_r_poweroff(void);
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef GKD_DEDICATED_RECOVERY
#define GKD_DEDICATED_RECOVERY 0
#endif

#define DEFAULT_CONFIG "/etc/gkd-mini/gdkmini.ui.conf"
#define RAM_OVERRIDE "/run/gkd-ui/gdkmini.override.conf"
#define RAM_FONT "/run/gkd-ui/ui.psf"
#define INPUT_ROOT "/dev/input"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#ifndef GKD_RECOVERY_BRIGHTNESS_PATH
#define GKD_RECOVERY_BRIGHTNESS_PATH "/sys/class/backlight/gkd350-backlight/brightness"
#endif

struct runtime {
	struct gkd_ui_config config;
	struct gkd_ui_font font;
	struct gkd_ui_surface surface;
	int fb_fd;
	int result_notice;
#if GKD_DEDICATED_RECOVERY
	struct gkd_input_owner input;
	uint16_t *draw_buffer;
	size_t frame_bytes;
#else
	int input_fd;
#endif
	void *mapping;
	size_t mapping_size;
};

#if GKD_DEDICATED_RECOVERY
static int run_program(const char *program, char *const argv[])
{
	char *envp[] = {
		"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", "TERM=linux", NULL
	};
	pid_t child, waited;
	int status = 0;
	child = fork();
	if (child < 0) return -1;
	if (!child) { execve(program, argv, envp); _exit(127); }
	do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
	return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

#endif

static int load_runtime_config(struct runtime *runtime)
{
	const char *font_path;
	gkd_ui_config_defaults(&runtime->config);
	if (gkd_ui_config_load(&runtime->config, DEFAULT_CONFIG, 0) < 0)
		return -1;
	if (access(RAM_OVERRIDE, R_OK) == 0)
		(void)gkd_ui_config_load(&runtime->config, RAM_OVERRIDE, 1);
	font_path = runtime->config.font_path;
	if (access(RAM_FONT, R_OK) == 0 &&
	    gkd_ui_font_load(&runtime->font, RAM_FONT) == 0)
		return 0;
	return gkd_ui_font_load(&runtime->font, font_path);
}

static int open_framebuffer(struct runtime *runtime)
{
	struct fb_fix_screeninfo fixed;
	struct fb_var_screeninfo variable;
	runtime->fb_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (runtime->fb_fd < 0 ||
	    ioctl(runtime->fb_fd, FBIOGET_FSCREENINFO, &fixed) < 0 ||
	    ioctl(runtime->fb_fd, FBIOGET_VSCREENINFO, &variable) < 0 ||
	    variable.xres != GKD_UI_WIDTH || variable.yres != GKD_UI_HEIGHT ||
	    variable.bits_per_pixel != 16U || fixed.line_length < GKD_UI_WIDTH * 2U ||
	    fixed.smem_len < fixed.line_length * GKD_UI_HEIGHT)
		return -1;
	runtime->mapping_size = fixed.smem_len;
	runtime->mapping = mmap(NULL, runtime->mapping_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, runtime->fb_fd, 0);
	if (runtime->mapping == MAP_FAILED) {
		runtime->mapping = NULL; return -1;
	}
	runtime->surface.pixels = runtime->mapping;
#if GKD_DEDICATED_RECOVERY
	/* Never expose clear_crt() or partially drawn widgets to scanout. */
	runtime->frame_bytes = (size_t)fixed.line_length * GKD_UI_HEIGHT;
	runtime->draw_buffer = calloc(1, runtime->frame_bytes);
	if (!runtime->draw_buffer) return -1;
	runtime->surface.pixels = runtime->draw_buffer;
#endif
	runtime->surface.width = GKD_UI_WIDTH;
	runtime->surface.height = GKD_UI_HEIGHT;
	runtime->surface.stride = fixed.line_length / 2U;
	return 0;
}

#if !GKD_DEDICATED_RECOVERY
static int physical_input(const char *path)
{
	char name[128] = {0};
	unsigned long event_bits[(EV_MAX + 8U * sizeof(unsigned long)) /
		(8U * sizeof(unsigned long))];
	unsigned long key_bits[(KEY_MAX + 8U * sizeof(unsigned long)) /
		(8U * sizeof(unsigned long))];
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) return -1;
	memset(event_bits, 0, sizeof(event_bits));
	memset(key_bits, 0, sizeof(key_bits));
	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
	    strcmp(name, "gpio-keys") ||
	    ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
	    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 ||
	    !(event_bits[EV_KEY / (8U * sizeof(unsigned long))] &
	      (1UL << (EV_KEY % (8U * sizeof(unsigned long))))) ||
	    !(key_bits[KEY_UP / (8U * sizeof(unsigned long))] &
	      (1UL << (KEY_UP % (8U * sizeof(unsigned long))))) ||
	    !(key_bits[KEY_LEFTCTRL / (8U * sizeof(unsigned long))] &
	      (1UL << (KEY_LEFTCTRL % (8U * sizeof(unsigned long)))))) {
		close(fd); return -1;
	}
	return fd;
}

static int open_input(void)
{
	DIR *directory;
	struct dirent *entry;
	int found = -1, count = 0;
	directory = opendir(INPUT_ROOT);
	if (!directory) return -1;
	while ((entry = readdir(directory))) {
		char path[256];
		int fd;
		if (strncmp(entry->d_name, "event", 5)) continue;
		if (snprintf(path, sizeof(path), "%s/%s", INPUT_ROOT, entry->d_name) >=
		    (int)sizeof(path)) continue;
		fd = physical_input(path);
		if (fd < 0) continue;
		++count;
		if (found >= 0) close(found);
		found = fd;
	}
	closedir(directory);
	if (count != 1 || found < 0 || ioctl(found, EVIOCGRAB, 1) < 0) {
		if (found >= 0) close(found);
		return -1;
	}
	return found;
}

static int next_key(int fd)
{
	struct pollfd poller = {fd, POLLIN, 0};
	struct input_event events[16];
	for (;;) {
		ssize_t count;
		unsigned i;
		if (poll(&poller, 1, -1) <= 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		count = read(fd, events, sizeof(events));
		if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
		if (count <= 0 || count % (ssize_t)sizeof(events[0])) return -1;
		for (i = 0; i < (unsigned)(count / (ssize_t)sizeof(events[0])); ++i)
			if (events[i].type == EV_KEY && events[i].value == 1)
				return events[i].code;
	}
}
#define RUNTIME_NEXT_KEY(runtime) next_key((runtime)->input_fd)
#else
#define RUNTIME_NEXT_KEY(runtime) gkd_input_owner_next_key(&(runtime)->input)
#endif

static int cycle_recovery_brightness(void)
{
	static const unsigned steps[]={2,10,20,30,40,50,60,70,80,90,100};
	char text[16],*end;unsigned next=steps[0];ssize_t n;
	int fd=open(GKD_RECOVERY_BRIGHTNESS_PATH,O_RDWR|O_CLOEXEC|O_NOFOLLOW);
	if(fd<0)return -1;
	n=pread(fd,text,sizeof(text)-1U,0);
	if(n<=0||n>=(ssize_t)sizeof(text)){close(fd);errno=EIO;return -1;}
	text[n]=0;unsigned long current=strtoul(text,&end,10);
	if(end==text||(*end&&(*end!='\n'||end[1]))||!current||current>100U){close(fd);errno=EPROTO;return -1;}
	for(unsigned i=0;i<ARRAY_SIZE(steps);i++)if(steps[i]>current){next=steps[i];break;}
	n=snprintf(text,sizeof(text),"%u",next);
	int result=n>0&&n<(ssize_t)sizeof(text)&&pwrite(fd,text,(size_t)n,0)==n?0:-1;
	int saved=result?(errno?errno:EIO):0;close(fd);errno=saved;return result;
}

static void present(struct runtime *runtime)
{
#if GKD_DEDICATED_RECOVERY
	/* The physical framebuffer receives only complete composed frames. */
	memcpy(runtime->mapping, runtime->draw_buffer, runtime->frame_bytes);
#endif
	__sync_synchronize();
	(void)fsync(runtime->fb_fd);
}

#if GKD_DEDICATED_RECOVERY
#define GKD_RECOVERY_USB_PROGRAM "/usr/sbin/gkd-recovery-usb"
#define GKD_RECOVERY_USB_START "export-start"
#define GKD_RECOVERY_USB_STOP "export-stop"
#else
#define GKD_RECOVERY_USB_PROGRAM "/usr/sbin/gkd-recovery-mass-storage"
#define GKD_RECOVERY_USB_START "start"
#define GKD_RECOVERY_USB_STOP "stop"
#endif


static int run_power_action(unsigned action)
{
	switch (action) {
	case 3:
#if GKD_DEDICATED_RECOVERY
		{
			char *stop[] = {GKD_RECOVERY_USB_PROGRAM, "stop", NULL};
			if (run_program(stop[0], stop) < 0) return -1;
		}
#endif
		sync();
		return reboot(RB_AUTOBOOT);
	case 4:
#if GKD_DEDICATED_RECOVERY
		return gkd_r_poweroff();
#else
		sync();
		return reboot(RB_POWER_OFF);
#endif
	default: errno = EINVAL; return -1;
	}
}

static int run_loading(struct runtime *runtime, const char *program,
		       char *const argv[], unsigned power_action)
{
	char *envp[] = {
		"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", "TERM=linux", NULL
	};
	struct timespec delay;
	pid_t child, waited;
	unsigned frame = 0;
	int status = 0;
	delay.tv_sec = (time_t)(runtime->config.loading_interval_ms / 1000U);
	delay.tv_nsec = (long)(runtime->config.loading_interval_ms % 1000U) * 1000000L;
	/* Publish before any blocking work, including a fast child completion. */
	gkd_ui_render_loading(&runtime->surface, &runtime->config,
		&runtime->font, frame++);
	present(runtime);
	child = fork();
	if (child < 0) return -1;
	if (!child) {
		if (power_action) _exit(run_power_action(power_action) == 0 ? 0 : 1);
		execve(program, argv, envp);
		_exit(127);
	}
	for (;;) {
		waited = waitpid(child, &status, WNOHANG);
		if (waited == child) break;
		if (waited < 0 && errno != EINTR) {
			(void)kill(child, SIGKILL);
			(void)waitpid(child, &status, 0);
			return -1;
		}
		gkd_ui_render_loading(&runtime->surface, &runtime->config,
			&runtime->font, frame++);
		present(runtime);
		while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
		delay.tv_sec = (time_t)(runtime->config.loading_interval_ms / 1000U);
		delay.tv_nsec = (long)(runtime->config.loading_interval_ms % 1000U) * 1000000L;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int run_program_loading(struct runtime *runtime, const char *program,
			       char *const argv[])
{
	return run_loading(runtime, program, argv, 0U);
}

static int wait_for_b(struct runtime *runtime)
{
	int key;
	for (;;) {
		key = RUNTIME_NEXT_KEY(runtime);
		if (key < 0) return -1;
		if (key == KEY_END) { (void)cycle_recovery_brightness(); continue; }
		if (key == KEY_LEFTALT || key == KEY_ESC) return 0;
	}
}


static int action_export(struct runtime *runtime)
{
	char *start[] = {GKD_RECOVERY_USB_PROGRAM, GKD_RECOVERY_USB_START, NULL};
	char *stop[] = {GKD_RECOVERY_USB_PROGRAM, GKD_RECOVERY_USB_STOP, NULL};
	int result;
	if (run_program_loading(runtime, start[0], start) < 0) return -1;
	gkd_ui_render_status(&runtime->surface, &runtime->config, &runtime->font,
			     "SYSTEM CARD EXPORTED", 0);
	present(runtime);
	result = wait_for_b(runtime);
	if (run_program_loading(runtime, stop[0], stop) < 0) result = -1;
	return result;
}

static int action_recovery(struct runtime *runtime, const char *action)
{
	char *argv[] = {"/usr/sbin/gkd-recovery", (char *)action, NULL};
	return run_program_loading(runtime, argv[0], argv);
}

static int run_action(struct runtime *runtime, unsigned selected)
{
	int result = -1;
	selected = gkd_ui_menu_action(selected);
	switch (selected) {
	case 0: result = action_export(runtime); break;
	case 1: result = action_recovery(runtime, "update"); break;
#if !GKD_DEDICATED_RECOVERY
	case 2: result = action_recovery(runtime, "rollback"); break;
#endif
	case 3:
	case 4:
		result = run_loading(runtime, NULL, NULL, selected);
		break;
	default: break;
	}
#if GKD_DEDICATED_RECOVERY
	if (result < 0) {
#else
	if (selected < 3U && result < 0) {
#endif
		/* Return to the menu; the existing failure result uses the shared OSD. */
		runtime->result_notice = 1;
	}
	return result;
}

static int confirm_action(struct runtime *runtime, unsigned selected)
{
	int key;
	gkd_ui_render_confirmation(&runtime->surface, &runtime->config,
		&runtime->font, runtime->config.labels[gkd_ui_menu_action(selected)]);
	present(runtime);
	for (;;) {
		key = RUNTIME_NEXT_KEY(runtime);
		if (key < 0) return -1;
		if (key == KEY_END) { (void)cycle_recovery_brightness(); continue; }
		if (key == KEY_LEFTCTRL || key == KEY_ENTER)
			return run_action(runtime, selected);
		if (key == KEY_LEFTALT || key == KEY_ESC) return 0;
	}
}

static int render_result_notice(struct runtime *runtime, unsigned selected)
{
	const struct gkd_ui_osd message = {"FAILED", GKD_UI_OSD_ICON_FAILURE, -1, 1};
	gkd_ui_render_recovery_menu(&runtime->surface, &runtime->config,
		&runtime->font, selected);
	return gkd_ui_draw_osd(&runtime->surface, &runtime->config,
		&runtime->font, &message, 255U);
}

static int device_main(void)
{
	struct runtime runtime;
	unsigned selected = 0;
	int key, result = 1;
	memset(&runtime, 0, sizeof(runtime));
#if GKD_DEDICATED_RECOVERY
	runtime.fb_fd = -1;
	gkd_input_owner_init(&runtime.input);
	if (load_runtime_config(&runtime) < 0 || open_framebuffer(&runtime) < 0 ||
	    gkd_input_owner_open(&runtime.input) < 0)
		goto out;
#else
	runtime.fb_fd = runtime.input_fd = -1;
	if (load_runtime_config(&runtime) < 0 || open_framebuffer(&runtime) < 0 ||
	    (runtime.input_fd = open_input()) < 0)
		goto out;
#endif
	for (;;) {
		gkd_ui_render_recovery_menu(&runtime.surface, &runtime.config,
			&runtime.font, selected);
		if (runtime.result_notice && render_result_notice(&runtime, selected) < 0) goto out;
		present(&runtime);
#if GKD_DEDICATED_RECOVERY
		key = runtime.result_notice ? gkd_input_owner_next_key_timeout(&runtime.input, 2000) :
			RUNTIME_NEXT_KEY(&runtime);
#else
		key = RUNTIME_NEXT_KEY(&runtime);
#endif
		runtime.result_notice = 0;
		if (key < 0) goto out;
		if (key == 0) continue;
		if (key == KEY_END) { (void)cycle_recovery_brightness(); continue; }
		if (key == KEY_UP) selected = (selected + GKD_UI_MENU_ITEMS - 1U) %
			GKD_UI_MENU_ITEMS;
		else if (key == KEY_DOWN) selected = (selected + 1U) %
			GKD_UI_MENU_ITEMS;
		else if (key == KEY_LEFTCTRL || key == KEY_ENTER)
			(void)confirm_action(&runtime, selected);
	}
out:
#if GKD_DEDICATED_RECOVERY
	(void)gkd_input_owner_close(&runtime.input);
	free(runtime.draw_buffer);
#else
	if (runtime.input_fd >= 0) {
		(void)ioctl(runtime.input_fd, EVIOCGRAB, 0);
		(void)close(runtime.input_fd);
	}
#endif
	if (runtime.mapping) (void)munmap(runtime.mapping, runtime.mapping_size);
	if (runtime.fb_fd >= 0) (void)close(runtime.fb_fd);
	gkd_ui_font_release(&runtime.font);
	return result;
}

static int render_test(const char *config_path, const char *font_path,
		       const char *output, const char *scene, unsigned frame)
{
	struct gkd_ui_config config;
	struct gkd_ui_font font;
	struct gkd_ui_surface surface;
	uint16_t *pixels;
	int result;
	gkd_ui_config_defaults(&config);
	if (gkd_ui_config_load(&config, config_path, 0) < 0 ||
	    gkd_ui_font_load(&font, font_path) < 0) return 2;
	pixels = calloc(GKD_UI_WIDTH * GKD_UI_HEIGHT, sizeof(*pixels));
	if (!pixels) { gkd_ui_font_release(&font); return 2; }
	surface = (struct gkd_ui_surface){
		pixels, GKD_UI_WIDTH, GKD_UI_HEIGHT, GKD_UI_WIDTH
	};
	if (!strcmp(scene, "menu") && frame < GKD_UI_MENU_ITEMS)
		gkd_ui_render_recovery_menu(&surface, &config, &font, frame);
	else if (!strcmp(scene, "confirmation"))
		gkd_ui_render_confirmation(&surface, &config, &font,
			config.labels[gkd_ui_menu_action(frame % GKD_UI_MENU_ITEMS)]);
	else if (!strcmp(scene, "status"))
		gkd_ui_render_status(&surface, &config, &font,
			"SYSTEM CARD EXPORTED", 0);
	else if (!strcmp(scene, "failure")) {
		struct runtime preview; memset(&preview, 0, sizeof(preview));
		preview.surface = surface; preview.config = config; preview.font = font;
		if (render_result_notice(&preview, 0U) < 0) { free(pixels); gkd_ui_font_release(&font); return 2; }
	}
	else if (!strcmp(scene, "loading"))
		gkd_ui_render_loading(&surface, &config, &font, frame);
	else {
		free(pixels); gkd_ui_font_release(&font); return 2;
	}
	result = gkd_ui_write_raw(&surface, output);
	free(pixels); gkd_ui_font_release(&font);
	return result ? 2 : 0;
}

int main(int argc, char **argv)
{
	if (argc == 7 && !strcmp(argv[1], "--render-test")) {
		char *end = NULL;
		unsigned long frame = strtoul(argv[6], &end, 10);
		if (!end || *end || frame > 255U) return 2;
		return render_test(argv[2], argv[3], argv[4], argv[5],
			(unsigned)frame);
	}
	if (argc != 1) {
		fprintf(stderr, "usage: gkd-recovery-ui [--render-test CONFIG FONT OUTPUT SCENE FRAME]\n");
		return 64;
	}
	return device_main();
}
