// SPDX-License-Identifier: GPL-2.0
#include "gkd-controls-state.h"

#include <limits.h>
#include <string.h>

#define GKD_CONTROLS_ALL_KEYS ((uint8_t)((1U << GKD_CONTROLS_KEY_COUNT) - 1U))
#define GKD_CONTROLS_INITIAL_DELAY_MS UINT64_C(400)
#define GKD_CONTROLS_REPEAT_INTERVAL_MS UINT64_C(100)

static uint8_t aggregate_held(const struct gkd_controls_state *state)
{
	return (uint8_t)(state->held[GKD_CONTROLS_SOURCE_PHYSICAL] |
		state->held[GKD_CONTROLS_SOURCE_VIRTUAL]);
}

static int note_time(struct gkd_controls_state *state, uint64_t now_ms)
{
	if (state->clock_valid && now_ms < state->last_now_ms) return -1;
	state->last_now_ms = now_ms;
	state->clock_valid = true;
	return 0;
}

static int deadline_after(uint64_t now_ms, uint64_t delay_ms,
	uint64_t *deadline_ms)
{
	if (now_ms > UINT64_MAX - delay_ms) return -1;
	*deadline_ms = now_ms + delay_ms;
	return 0;
}

static void clear_released_suppression(struct gkd_controls_state *state)
{
	state->suppressed &= aggregate_held(state);
}

static bool volumes_held(const struct gkd_controls_state *state)
{
	return (aggregate_held(state) &
		(GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP) |
		 GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_DOWN))) != 0U;
}

static bool opposing_volumes_held(const struct gkd_controls_state *state)
{
	uint8_t volume_mask = GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP) |
		GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_DOWN);
	return (aggregate_held(state) & volume_mask) == volume_mask;
}

static void cancel_repeat(struct gkd_controls_state *state)
{
	state->repeat_active = false;
}

/* A runtime fault must not leave an already-held key able to act later. */
static void quarantine_held_inputs(struct gkd_controls_state *state)
{
	cancel_repeat(state);
	state->suppressed |= aggregate_held(state);
}

static int invoke_effect(struct gkd_controls_state *state,
	enum gkd_controls_action action, int delta)
{
	return state->effect(state->context, action, delta) == 0 ? 0 : -1;
}

int gkd_controls_init(struct gkd_controls_state *state,
	const struct gkd_controls_config *config, gkd_controls_effect_fn effect,
	void *context)
{
	if (!state || !config || !effect || config->volume_step == 0U ||
	    config->volume_step > 100U)
		return -1;
	memset(state, 0, sizeof(*state));
	state->config = *config;
	state->effect = effect;
	state->context = context;
	return 0;
}

int gkd_controls_event(struct gkd_controls_state *state,
	enum gkd_controls_source source, enum gkd_controls_key key, int value,
	uint64_t now_ms)
{
	uint8_t key_mask, before, after;
	bool transitioned;
	int delta;
	if (!state || source < GKD_CONTROLS_SOURCE_PHYSICAL ||
	    source >= GKD_CONTROLS_SOURCE_COUNT || key < GKD_CONTROLS_KEY_VOLUME_UP ||
	    key >= GKD_CONTROLS_KEY_COUNT || (value != 0 && value != 1 && value != 2))
		return -1;
	if (note_time(state, now_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	if (value == 2) return 0;
	key_mask = (uint8_t)GKD_CONTROLS_KEY_MASK(key);
	before = aggregate_held(state);
	if (value == 0)
		state->held[source] &= (uint8_t)~key_mask;
	else
		state->held[source] |= key_mask;
	after = aggregate_held(state);
	transitioned = (before & key_mask) == 0U && (after & key_mask) != 0U;
	clear_released_suppression(state);
	if (key == GKD_CONTROLS_KEY_VOLUME_UP || key == GKD_CONTROLS_KEY_VOLUME_DOWN) {
		if (opposing_volumes_held(state)) {
			state->volume_inhibited = true;
			cancel_repeat(state);
		}
		if (!volumes_held(state)) state->volume_inhibited = false;
		if (state->repeat_active &&
		    (after & (state->repeat_delta > 0 ?
		    GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP) :
		    GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_DOWN))) == 0U)
			cancel_repeat(state);
	}
	if (state->blocked) {
		state->suppressed |= after;
		cancel_repeat(state);
		return 0;
	}
	if (!transitioned || (state->suppressed & key_mask) != 0U) return 0;
	if (key == GKD_CONTROLS_KEY_BRIGHTNESS) {
		if (invoke_effect(state, GKD_CONTROLS_ACTION_BRIGHTNESS, 0) != 0) {
			quarantine_held_inputs(state);
			return -1;
		}
		return 0;
	}
	if (state->volume_inhibited) return 0;
	delta = key == GKD_CONTROLS_KEY_VOLUME_UP ? (int)state->config.volume_step :
		-(int)state->config.volume_step;
	if (deadline_after(now_ms, GKD_CONTROLS_INITIAL_DELAY_MS,
	    &state->repeat_deadline_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	if (invoke_effect(state, GKD_CONTROLS_ACTION_VOLUME, delta) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	state->repeat_delta = delta;
	state->repeat_active = true;
	return 0;
}

int gkd_controls_tick(struct gkd_controls_state *state, uint64_t now_ms)
{
	uint8_t required;
	if (!state) return -1;
	if (note_time(state, now_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	if (!state->repeat_active || state->blocked || state->volume_inhibited ||
	    now_ms < state->repeat_deadline_ms)
		return 0;
	required = (uint8_t)(state->repeat_delta > 0 ?
		GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_UP) :
		GKD_CONTROLS_KEY_MASK(GKD_CONTROLS_KEY_VOLUME_DOWN));
	if ((aggregate_held(state) & required) == 0U) {
		cancel_repeat(state);
		return 0;
	}
	if (deadline_after(now_ms, GKD_CONTROLS_REPEAT_INTERVAL_MS,
	    &state->repeat_deadline_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	if (invoke_effect(state, GKD_CONTROLS_ACTION_VOLUME,
	    state->repeat_delta) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	return 0;
}

int gkd_controls_resync(struct gkd_controls_state *state,
	enum gkd_controls_source source, uint32_t held_mask, uint64_t now_ms)
{
	if (!state || source < GKD_CONTROLS_SOURCE_PHYSICAL ||
	    source >= GKD_CONTROLS_SOURCE_COUNT ||
	    (held_mask & ~(uint32_t)GKD_CONTROLS_ALL_KEYS) != 0U)
		return -1;
	if (note_time(state, now_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	state->held[source] = (uint8_t)held_mask;
	cancel_repeat(state);
	state->suppressed |= aggregate_held(state);
	clear_released_suppression(state);
	if (opposing_volumes_held(state)) state->volume_inhibited = true;
	if (!volumes_held(state)) state->volume_inhibited = false;
	return 0;
}

int gkd_controls_set_blocked(struct gkd_controls_state *state, bool blocked,
	uint64_t now_ms)
{
	if (!state) return -1;
	if (note_time(state, now_ms) != 0) {
		quarantine_held_inputs(state);
		return -1;
	}
	if (blocked) {
		state->suppressed |= aggregate_held(state);
		cancel_repeat(state);
	}
	state->blocked = blocked;
	return 0;
}

int64_t gkd_controls_timeout_ms(const struct gkd_controls_state *state,
	uint64_t now_ms)
{
	uint64_t remaining;
	if (!state || (state->clock_valid && now_ms < state->last_now_ms) ||
	    !state->repeat_active || state->blocked || state->volume_inhibited)
		return -1;
	if (now_ms >= state->repeat_deadline_ms) return 0;
	remaining = state->repeat_deadline_ms - now_ms;
	return remaining > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)remaining;
}
