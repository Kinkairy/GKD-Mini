// SPDX-License-Identifier: GPL-2.0
#ifndef GKD_INPUT_KEYS_H
#define GKD_INPUT_KEYS_H
#include <linux/input.h>
#include <string.h>
static inline int gkd_input_key_code(const char *name, unsigned short *code)
{
	static const struct { const char *name; unsigned short code; } values[] = {
		{"KEY_ESC", KEY_ESC}, {"KEY_BACKSPACE", KEY_BACKSPACE},
		{"KEY_TAB", KEY_TAB}, {"KEY_ENTER", KEY_ENTER},
		{"KEY_LEFTCTRL", KEY_LEFTCTRL}, {"KEY_LEFTSHIFT", KEY_LEFTSHIFT},
		{"KEY_LEFTALT", KEY_LEFTALT}, {"KEY_SPACE", KEY_SPACE},
		{"KEY_KPMINUS", KEY_KPMINUS}, {"KEY_KPPLUS", KEY_KPPLUS},
		{"KEY_HOME", KEY_HOME}, {"KEY_UP", KEY_UP},
		{"KEY_PAGEUP", KEY_PAGEUP}, {"KEY_LEFT", KEY_LEFT},
		{"KEY_RIGHT", KEY_RIGHT}, {"KEY_END", KEY_END},
		{"KEY_DOWN", KEY_DOWN}, {"KEY_PAGEDOWN", KEY_PAGEDOWN},
	};
	unsigned int i;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		if (!strcmp(name, values[i].name)) {
			*code = values[i].code;
			return 0;
		}
	}
	return -1;
}

#endif
