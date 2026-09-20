// SPDX-License-Identifier: GPL-2.0
#include "gkd-controls-state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct effect_log {
	enum gkd_controls_action action[32];
	int delta[32];
	unsigned int count;
	unsigned int fail_at;
};

static int effect(void *context, enum gkd_controls_action action, int delta)
{
	struct effect_log *log = context;
	if (log->count >= sizeof(log->action) / sizeof(log->action[0])) return -1;
	log->action[log->count] = action;
	log->delta[log->count] = delta;
	++log->count;
	return log->fail_at == log->count ? -1 : 0;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		(void)fprintf(stderr, "%s:%d: %s\\n", __FILE__, __LINE__, #expression); \
		return -1; \
	} \
} while (0)

static int init(struct gkd_controls_state *state, struct effect_log *log)
{
	struct gkd_controls_config config = {7U};
	memset(log, 0, sizeof(*log));
	return gkd_controls_init(state, &config, effect, log);
}

static int overlap_and_deadlines(void)
{
	struct gkd_controls_state state;
	struct effect_log log;
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 10U) == 0);
	CHECK(log.count == 1U && log.action[0] == GKD_CONTROLS_ACTION_VOLUME &&
		log.delta[0] == 7);
	CHECK(gkd_controls_timeout_ms(&state, 10U) == 400);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 11U) == 0 && log.count == 1U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 2, 12U) == 0 && log.count == 1U);
	CHECK(gkd_controls_tick(&state, 409U) == 0 && log.count == 1U);
	CHECK(gkd_controls_tick(&state, 410U) == 0 && log.count == 2U);
	CHECK(gkd_controls_timeout_ms(&state, 1000U) == 0);
	CHECK(gkd_controls_tick(&state, 1000U) == 0 && log.count == 3U);
	CHECK(gkd_controls_timeout_ms(&state, 1000U) == 100);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 1001U) == 0);
	CHECK(gkd_controls_tick(&state, 1100U) == 0 && log.count == 4U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 1101U) == 0);
	CHECK(gkd_controls_timeout_ms(&state, 1101U) == -1);
	return 0;
}

static int opposing_keys_and_brightness(void)
{
	struct gkd_controls_state state;
	struct effect_log log;
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 0U) == 0 && log.count == 1U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 1, 1U) == 0 && log.count == 1U);
	CHECK(gkd_controls_tick(&state, 500U) == 0 && log.count == 1U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 501U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 0, 502U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 1, 503U) == 0 && log.count == 2U &&
		log.delta[1] == -7);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 1, 504U) == 0 && log.count == 3U &&
		log.action[2] == GKD_CONTROLS_ACTION_BRIGHTNESS);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 1, 505U) == 0 && log.count == 3U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 0, 506U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 0, 507U) == 0);
	return 0;
}

static int resync_and_blocked(void)
{
	struct gkd_controls_state state;
	struct effect_log log;
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_resync(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP), 10U) == 0);
	CHECK(gkd_controls_resync(&state, GKD_CONTROLS_SOURCE_VIRTUAL, 0U, 10U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 2, 11U) == 0 && log.count == 0U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 12U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 13U) == 0 && log.count == 1U);
	CHECK(gkd_controls_set_blocked(&state, true, 14U) == 0);
	CHECK(gkd_controls_set_blocked(&state, false, 15U) == 0);
	CHECK(gkd_controls_tick(&state, 500U) == 0 && log.count == 1U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 501U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 502U) == 0 && log.count == 2U);
	CHECK(gkd_controls_set_blocked(&state, true, 503U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 1, 504U) == 0 && log.count == 2U);
	CHECK(gkd_controls_set_blocked(&state, false, 505U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 0, 506U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 1, 507U) == 0 && log.count == 3U);
	return 0;
}

static int failures_and_invalid_input(void)
{
	struct gkd_controls_state state;
	struct effect_log log;
	struct gkd_controls_config bad = {0U};
	CHECK(gkd_controls_init(&state, &bad, effect, &log) < 0);
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 20U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 19U) < 0);
	CHECK(gkd_controls_timeout_ms(&state, 19U) == -1);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_COUNT,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 21U) < 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_COUNT, 1, 21U) < 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 3, 21U) < 0);
	CHECK(gkd_controls_resync(&state, GKD_CONTROLS_SOURCE_PHYSICAL, 8U, 21U) < 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 21U) == 0);
	log.fail_at = log.count + 1U;
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 1, 22U) < 0);
	CHECK(gkd_controls_timeout_ms(&state, 22U) == -1);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 0, 23U) == 0);
	log.fail_at = 0U;
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_DOWN, 1, 24U) == 0);
	log.fail_at = log.count + 1U;
	CHECK(gkd_controls_tick(&state, 424U) < 0);
	CHECK(gkd_controls_timeout_ms(&state, 424U) == -1);
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, UINT64_MAX) < 0 && log.count == 0U);
	return 0;
}

static int runtime_faults_quarantine_held_inputs(void)
{
	struct gkd_controls_state state;
	struct effect_log log;
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 100U) == 0 && log.count == 1U);
	CHECK(gkd_controls_tick(&state, 99U) < 0);
	CHECK(gkd_controls_tick(&state, 500U) == 0 && log.count == 1U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 500U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 501U) == 0 && log.count == 2U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 502U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 600U) == 0 && log.count == 3U);
	log.fail_at = 4U;
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 1, 601U) < 0 && log.count == 4U);
	log.fail_at = 0U;
	CHECK(gkd_controls_tick(&state, 1000U) == 0 && log.count == 4U);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_VIRTUAL,
		GKD_CONTROLS_KEY_BRIGHTNESS, 0, 1001U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 0, 1002U) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, 1003U) == 0 && log.count == 5U);
	CHECK(init(&state, &log) == 0);
	CHECK(gkd_controls_event(&state, GKD_CONTROLS_SOURCE_PHYSICAL,
		GKD_CONTROLS_KEY_VOLUME_UP, 1, UINT64_MAX - 500U) == 0 &&
		log.count == 1U);
	CHECK(gkd_controls_tick(&state, UINT64_MAX) < 0 && log.count == 1U);
	CHECK(gkd_controls_tick(&state, UINT64_MAX) == 0 && log.count == 1U);
	return 0;
}

int main(void)
{
	if (overlap_and_deadlines() || opposing_keys_and_brightness() ||
	    resync_and_blocked() || failures_and_invalid_input() ||
	    runtime_faults_quarantine_held_inputs())
		return 1;
	return 0;
}
