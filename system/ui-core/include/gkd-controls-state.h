// SPDX-License-Identifier: GPL-2.0
#ifndef GKD_CONTROLS_STATE_H
#define GKD_CONTROLS_STATE_H

#include <stdbool.h>
#include <stdint.h>

enum gkd_controls_source {
	GKD_CONTROLS_SOURCE_PHYSICAL = 0,
	GKD_CONTROLS_SOURCE_VIRTUAL = 1,
	GKD_CONTROLS_SOURCE_COUNT = 2
};

enum gkd_controls_key {
	GKD_CONTROLS_KEY_VOLUME_UP = 0,
	GKD_CONTROLS_KEY_VOLUME_DOWN = 1,
	GKD_CONTROLS_KEY_BRIGHTNESS = 2,
	GKD_CONTROLS_KEY_COUNT = 3
};

#define GKD_CONTROLS_KEY_MASK(key) (1U << (unsigned int)(key))

enum gkd_controls_action {
	GKD_CONTROLS_ACTION_VOLUME = 0,
	GKD_CONTROLS_ACTION_BRIGHTNESS = 1
};

struct gkd_controls_config {
	unsigned int volume_step;
};

typedef int (*gkd_controls_effect_fn)(void *context,
	enum gkd_controls_action action, int delta);

struct gkd_controls_state {
	struct gkd_controls_config config;
	gkd_controls_effect_fn effect;
	void *context;
	uint8_t held[GKD_CONTROLS_SOURCE_COUNT];
	uint8_t suppressed;
	uint64_t repeat_deadline_ms;
	uint64_t last_now_ms;
	int repeat_delta;
	bool blocked;
	bool volume_inhibited;
	bool repeat_active;
	bool clock_valid;
};

int gkd_controls_init(struct gkd_controls_state *state,
	const struct gkd_controls_config *config, gkd_controls_effect_fn effect,
	void *context);
int gkd_controls_event(struct gkd_controls_state *state,
	enum gkd_controls_source source, enum gkd_controls_key key, int value,
	uint64_t now_ms);
int gkd_controls_tick(struct gkd_controls_state *state, uint64_t now_ms);
int gkd_controls_resync(struct gkd_controls_state *state,
	enum gkd_controls_source source, uint32_t held_mask, uint64_t now_ms);
int gkd_controls_set_blocked(struct gkd_controls_state *state, bool blocked,
	uint64_t now_ms);
int64_t gkd_controls_timeout_ms(const struct gkd_controls_state *state,
	uint64_t now_ms);

#endif
