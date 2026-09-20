/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-app-lifecycle.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

enum lifecycle_flow {
    FLOW_NONE,
    FLOW_OPEN_MENU,
    FLOW_CANCEL_MENU,
    FLOW_CHARGE,
    FLOW_STORAGE_ENTER,
    FLOW_STORAGE_LEAVE,
    FLOW_DEBUG_ENTER,
    FLOW_DEBUG_LEAVE,
    FLOW_POWER_SUSPEND,
    FLOW_POWER_REBOOT,
    FLOW_POWER_SHUTDOWN,
    FLOW_POWER_RESUME,
    FLOW_APP_RESTART,
    FLOW_CARD_REFRESH
};

int gkd_app_lifecycle_usb_transition(const struct gkd_app_lifecycle *lifecycle)
{
    if (!lifecycle || lifecycle->state == GKD_LIFECYCLE_RECOVERY ||
        lifecycle->pending_action == GKD_LIFECYCLE_ACTION_MENU_RELEASE)
        return 0;
    return lifecycle->flow == FLOW_STORAGE_ENTER ||
        lifecycle->flow == FLOW_STORAGE_LEAVE ||
        lifecycle->flow == FLOW_DEBUG_ENTER ||
        lifecycle->flow == FLOW_DEBUG_LEAVE;
}

enum gkd_app_wait_kind gkd_app_lifecycle_wait_kind(const struct gkd_app_lifecycle *lifecycle)
{
    if (!lifecycle || lifecycle->state == GKD_LIFECYCLE_RECOVERY ||
        lifecycle->pending_action == GKD_LIFECYCLE_ACTION_MENU_RELEASE)
        return GKD_APP_WAIT_NONE;
    if (gkd_app_lifecycle_usb_transition(lifecycle) || lifecycle->flow == FLOW_CHARGE)
        return GKD_APP_WAIT_USB;
    switch (lifecycle->flow) {
    case FLOW_POWER_SUSPEND:
    case FLOW_POWER_REBOOT:
    case FLOW_POWER_SHUTDOWN:
    case FLOW_POWER_RESUME:
        return GKD_APP_WAIT_POWER;
    case FLOW_APP_RESTART:
        return GKD_APP_WAIT_APPLICATION;
    default:
        return GKD_APP_WAIT_NONE;
    }
}

static int advance(struct gkd_app_lifecycle *lifecycle);
static int start_flow(struct gkd_app_lifecycle *lifecycle,
    unsigned flow);

static int reject(int error)
{
    errno = error;
    return -1;
}

static int is_enter_flow(unsigned flow)
{
    return flow == FLOW_STORAGE_ENTER || flow == FLOW_DEBUG_ENTER;
}

static void record_failure(struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_action action, int error)
{
    lifecycle->pending_action = GKD_LIFECYCLE_ACTION_NONE;
    lifecycle->failed_action = action;
    lifecycle->last_error = error > 0 ? error : EIO;
    lifecycle->state = GKD_LIFECYCLE_RECOVERY;
}

static int emit(struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_action action)
{
    struct gkd_app_lifecycle_request request;
    uint64_t next;
    int saved;

    if (lifecycle->pending_action != GKD_LIFECYCLE_ACTION_NONE)
        return reject(EBUSY);
    next = lifecycle->pending_token + 1U;
    if (!next) {
        record_failure(lifecycle, action, EOVERFLOW);
        return reject(EOVERFLOW);
    }
    if (action == GKD_LIFECYCLE_ACTION_APP_START) {
        next = lifecycle->requested_generation + 1U;
        if (!next) {
            record_failure(lifecycle, action, EOVERFLOW);
            return reject(EOVERFLOW);
        }
        lifecycle->requested_generation = next;
    }

    lifecycle->pending_token++;
    request.action = action;
    request.token = lifecycle->pending_token;
    request.generation =
        action == GKD_LIFECYCLE_ACTION_APP_START ?
        lifecycle->requested_generation : 0U;
    lifecycle->pending_action = action;
    lifecycle->state =
        action == GKD_LIFECYCLE_ACTION_MENU_ACQUIRE ?
        GKD_LIFECYCLE_MENU_OPENING : GKD_LIFECYCLE_TRANSITION;
    lifecycle->dispatching = 1;
    errno = 0;
    if (!lifecycle->begin(lifecycle->opaque, &request)) {
        lifecycle->dispatching = 0;
        return 0;
    }
    saved = errno ? errno : EIO;
    lifecycle->dispatching = 0;
    record_failure(lifecycle, action, saved);
    errno = saved;
    return -1;
}

static enum gkd_app_lifecycle_action flow_action(
    const struct gkd_app_lifecycle *lifecycle)
{
    switch (lifecycle->flow) {
    case FLOW_OPEN_MENU:
        return lifecycle->step == 0U ?
            GKD_LIFECYCLE_ACTION_MENU_ACQUIRE :
            GKD_LIFECYCLE_ACTION_NONE;
    case FLOW_CANCEL_MENU:
        return lifecycle->step == 0U ?
            GKD_LIFECYCLE_ACTION_MENU_RELEASE :
            GKD_LIFECYCLE_ACTION_NONE;
    case FLOW_CHARGE:
        if (lifecycle->step == 0U)
            return GKD_LIFECYCLE_ACTION_MENU_RELEASE;
        return lifecycle->step == 1U ?
            GKD_LIFECYCLE_ACTION_USB_CHARGE :
            GKD_LIFECYCLE_ACTION_NONE;
    case FLOW_STORAGE_ENTER: {
        static const enum gkd_app_lifecycle_action actions[] = {
            GKD_LIFECYCLE_ACTION_MENU_RELEASE,
            GKD_LIFECYCLE_ACTION_APP_PAUSE,
            GKD_LIFECYCLE_ACTION_GAME_UNMOUNT,
            GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME
        };
        return lifecycle->step < sizeof(actions) / sizeof(actions[0]) ?
            actions[lifecycle->step] : GKD_LIFECYCLE_ACTION_NONE;
    }
    case FLOW_STORAGE_LEAVE: {
        static const enum gkd_app_lifecycle_action actions[] = {
            GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN,
            GKD_LIFECYCLE_ACTION_USB_FLUSH,
            GKD_LIFECYCLE_ACTION_GAME_MOUNT,
            GKD_LIFECYCLE_ACTION_APP_RESUME
        };
        return lifecycle->step < sizeof(actions) / sizeof(actions[0]) ?
            actions[lifecycle->step] : GKD_LIFECYCLE_ACTION_NONE;
    }
    case FLOW_DEBUG_ENTER: {
        static const enum gkd_app_lifecycle_action actions[] = {
            GKD_LIFECYCLE_ACTION_MENU_RELEASE,
            GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE,
            GKD_LIFECYCLE_ACTION_APP_STOP,
            GKD_LIFECYCLE_ACTION_P2_RELEASE,
            GKD_LIFECYCLE_ACTION_LOOPS_RELEASE,
            GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM
        };
        return lifecycle->step < sizeof(actions) / sizeof(actions[0]) ?
            actions[lifecycle->step] : GKD_LIFECYCLE_ACTION_NONE;
    }
    case FLOW_DEBUG_LEAVE: {
        static const enum gkd_app_lifecycle_action actions[] = {
            GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN,
            GKD_LIFECYCLE_ACTION_USB_FLUSH,
            GKD_LIFECYCLE_ACTION_SYSTEM_MEDIA_VALIDATE,
            GKD_LIFECYCLE_ACTION_APP_START,
            GKD_LIFECYCLE_ACTION_DISPLAY_THAW
        };
        return lifecycle->step < sizeof(actions) / sizeof(actions[0]) ?
            actions[lifecycle->step] : GKD_LIFECYCLE_ACTION_NONE;
    }
    case FLOW_POWER_SUSPEND:
    case FLOW_POWER_REBOOT:
    case FLOW_POWER_SHUTDOWN:
        if (lifecycle->step == 0U)
            return GKD_LIFECYCLE_ACTION_MENU_RELEASE;
        if (lifecycle->step == 1U)
            return GKD_LIFECYCLE_ACTION_POWER_QUIESCE;
        if (lifecycle->step == 2U)
            return GKD_LIFECYCLE_ACTION_POWER_CARD_RELEASE;
        if (lifecycle->step != 3U)
            return GKD_LIFECYCLE_ACTION_NONE;
        if (lifecycle->flow == FLOW_POWER_SUSPEND)
            return GKD_LIFECYCLE_ACTION_POWER_SUSPEND;
        if (lifecycle->flow == FLOW_POWER_REBOOT)
            return GKD_LIFECYCLE_ACTION_POWER_REBOOT;
        return GKD_LIFECYCLE_ACTION_POWER_SHUTDOWN;
    case FLOW_POWER_RESUME:
        return lifecycle->step == 0U ?
            GKD_LIFECYCLE_ACTION_POWER_RESUME :
            GKD_LIFECYCLE_ACTION_NONE;
    case FLOW_CARD_REFRESH: {
        static const enum gkd_app_lifecycle_action actions[]={
            GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE,
            GKD_LIFECYCLE_ACTION_APP_STOP,
            GKD_LIFECYCLE_ACTION_P2_RELEASE,
            GKD_LIFECYCLE_ACTION_LOOPS_RELEASE,
            GKD_LIFECYCLE_ACTION_APP_START,
            GKD_LIFECYCLE_ACTION_DISPLAY_THAW
        };
        return lifecycle->step < sizeof(actions)/sizeof(actions[0])?
            actions[lifecycle->step]:GKD_LIFECYCLE_ACTION_NONE;
    }
    case FLOW_APP_RESTART:
        if (lifecycle->step == 0U)
            return GKD_LIFECYCLE_ACTION_MENU_RELEASE;
        return lifecycle->step == 1U ?
            GKD_LIFECYCLE_ACTION_APP_START :
            GKD_LIFECYCLE_ACTION_NONE;
    default:
        return GKD_LIFECYCLE_ACTION_NONE;
    }
}

static void start_leave(struct gkd_app_lifecycle *lifecycle)
{
    lifecycle->flow =
        lifecycle->flow == FLOW_DEBUG_ENTER ||
        lifecycle->usb == GKD_LIFECYCLE_USB_SYSTEM ||
        lifecycle->display_frozen ?
        FLOW_DEBUG_LEAVE : FLOW_STORAGE_LEAVE;
    lifecycle->step = 0U;
    lifecycle->leave_requested = 0U;
    lifecycle->last_error = 0;
    lifecycle->failed_action = GKD_LIFECYCLE_ACTION_NONE;
}

static int finish_flow(struct gkd_app_lifecycle *lifecycle)
{
    unsigned flow = lifecycle->flow;

    lifecycle->flow = FLOW_NONE;
    lifecycle->step = 0U;
    lifecycle->failed_action = GKD_LIFECYCLE_ACTION_NONE;
    lifecycle->last_error = 0;
    switch (flow) {
    case FLOW_OPEN_MENU:
        lifecycle->state = GKD_LIFECYCLE_MENU;
        break;
    case FLOW_CANCEL_MENU:
    case FLOW_STORAGE_LEAVE:
    case FLOW_DEBUG_LEAVE:
    case FLOW_POWER_RESUME:
    case FLOW_CHARGE:
        lifecycle->state = GKD_LIFECYCLE_ACTIVE;
        if (!lifecycle->app_running)
            return start_flow(lifecycle, FLOW_APP_RESTART);
        break;
    case FLOW_CARD_REFRESH:
    case FLOW_APP_RESTART:
        lifecycle->state = GKD_LIFECYCLE_ACTIVE;
        break;
    case FLOW_STORAGE_ENTER:
        lifecycle->state = GKD_LIFECYCLE_STORAGE;
        break;
    case FLOW_DEBUG_ENTER:
        lifecycle->state = GKD_LIFECYCLE_DEBUG;
        break;
    case FLOW_POWER_SUSPEND:
        lifecycle->state = GKD_LIFECYCLE_SUSPENDED;
        break;
    case FLOW_POWER_REBOOT:
    case FLOW_POWER_SHUTDOWN:
        lifecycle->state = GKD_LIFECYCLE_TERMINAL;
        break;
    default:
        return reject(EPROTO);
    }
    return 0;
}

static int skip_action(const struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_action action)
{
    if (action == GKD_LIFECYCLE_ACTION_MENU_RELEASE)
        return !lifecycle->menu_owned;
    if (action == GKD_LIFECYCLE_ACTION_GAME_MOUNT)
        return lifecycle->game_media == GKD_LIFECYCLE_MEDIA_MOUNTED;
    if (action == GKD_LIFECYCLE_ACTION_APP_RESUME)
        return !lifecycle->app_paused;
    if (action == GKD_LIFECYCLE_ACTION_APP_START)
        return lifecycle->app_running && lifecycle->app_ready;
    if (action == GKD_LIFECYCLE_ACTION_DISPLAY_THAW)
        return !lifecycle->display_frozen;
    return 0;
}

static int advance(struct gkd_app_lifecycle *lifecycle)
{
    enum gkd_app_lifecycle_action action;

    if (lifecycle->pending_action != GKD_LIFECYCLE_ACTION_NONE)
        return 0;
    if (lifecycle->flow == FLOW_OPEN_MENU &&
        lifecycle->leave_requested && lifecycle->step) {
        lifecycle->flow = FLOW_CANCEL_MENU;
        lifecycle->step = 0U;
        lifecycle->leave_requested = 0U;
    }
    if (is_enter_flow(lifecycle->flow) && lifecycle->leave_requested)
        start_leave(lifecycle);
    if (lifecycle->waiting_ready) {
        lifecycle->state = GKD_LIFECYCLE_WAIT_APP_READY;
        return 0;
    }
    for (;;) {
        action = flow_action(lifecycle);
        if (action == GKD_LIFECYCLE_ACTION_NONE)
            return finish_flow(lifecycle);
        if (action == GKD_LIFECYCLE_ACTION_GAME_MOUNT &&
            lifecycle->game_media == GKD_LIFECYCLE_MEDIA_UNKNOWN) {
            lifecycle->probe_origin = action;
            return emit(lifecycle, GKD_LIFECYCLE_ACTION_GAME_PROBE);
        }
        if (!skip_action(lifecycle, action))
            return emit(lifecycle, action);
        lifecycle->step++;
    }
}

static void apply_success(struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_action action)
{
    switch (action) {
    case GKD_LIFECYCLE_ACTION_MENU_ACQUIRE:
        lifecycle->menu_owned = 1U;
        break;
    case GKD_LIFECYCLE_ACTION_MENU_RELEASE:
        lifecycle->menu_owned = 0U;
        break;
    case GKD_LIFECYCLE_ACTION_DISPLAY_FREEZE:
        lifecycle->display_frozen = 1U;
        break;
    case GKD_LIFECYCLE_ACTION_DISPLAY_THAW:
        lifecycle->display_frozen = 0U;
        break;
    case GKD_LIFECYCLE_ACTION_APP_PAUSE:
        lifecycle->app_paused = 1U;
        break;
    case GKD_LIFECYCLE_ACTION_GAME_UNMOUNT:
        lifecycle->game_media = GKD_LIFECYCLE_MEDIA_UNMOUNTED;
        break;
    case GKD_LIFECYCLE_ACTION_GAME_MOUNT:
        lifecycle->game_media = GKD_LIFECYCLE_MEDIA_MOUNTED;
        break;
    case GKD_LIFECYCLE_ACTION_APP_RESUME:
        lifecycle->app_paused = 0U;
        break;
    case GKD_LIFECYCLE_ACTION_APP_STOP:
        lifecycle->app_running = 0U;
        lifecycle->app_ready = 0U;
        lifecycle->app_paused = 0U;
        break;
    case GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME:
        lifecycle->usb = GKD_LIFECYCLE_USB_GAME;
        break;
    case GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM:
        lifecycle->usb = GKD_LIFECYCLE_USB_SYSTEM;
        break;
    case GKD_LIFECYCLE_ACTION_USB_CLEAR_LUN:
        lifecycle->usb = GKD_LIFECYCLE_USB_NONE;
        break;
    case GKD_LIFECYCLE_ACTION_APP_START:
        lifecycle->generation = lifecycle->requested_generation;
        lifecycle->app_running = 1U;
        lifecycle->app_ready = 0U;
        lifecycle->waiting_ready = 1U;
        break;
    case GKD_LIFECYCLE_ACTION_POWER_QUIESCE:
        lifecycle->power_quiesced = 1U;
        if (lifecycle->flow == FLOW_POWER_REBOOT ||
            lifecycle->flow == FLOW_POWER_SHUTDOWN) {
            lifecycle->app_running = 0U;
            lifecycle->app_ready = 0U;
            lifecycle->app_paused = 0U;
        }
        break;
    case GKD_LIFECYCLE_ACTION_POWER_RESUME:
        lifecycle->power_quiesced = 0U;
        break;
    default:
        break;
    }
}

static int start_flow(struct gkd_app_lifecycle *lifecycle, unsigned flow)
{
    lifecycle->flow = flow;
    lifecycle->step = 0U;
    lifecycle->last_error = 0;
    lifecycle->failed_action = GKD_LIFECYCLE_ACTION_NONE;
    return advance(lifecycle);
}

int gkd_app_lifecycle_init(struct gkd_app_lifecycle *lifecycle,
    uint64_t active_generation, gkd_app_lifecycle_begin_fn begin, void *opaque)
{
    if (!lifecycle || !active_generation || !begin)
        return reject(EINVAL);
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->state = GKD_LIFECYCLE_ACTIVE;
    lifecycle->game_media = GKD_LIFECYCLE_MEDIA_MOUNTED;
    lifecycle->usb = GKD_LIFECYCLE_USB_NONE;
    lifecycle->generation = active_generation;
    lifecycle->requested_generation = active_generation;
    lifecycle->app_running = 1U;
    lifecycle->app_ready = 1U;
    lifecycle->begin = begin;
    lifecycle->opaque = opaque;
    return 0;
}

static int leave_event(struct gkd_app_lifecycle *lifecycle)
{
    if (lifecycle->flow == FLOW_STORAGE_LEAVE ||
        lifecycle->flow == FLOW_DEBUG_LEAVE)
        return 0;
    if (lifecycle->flow == FLOW_OPEN_MENU) {
        lifecycle->leave_requested = 1U;
        return advance(lifecycle);
    }
    if (is_enter_flow(lifecycle->flow)) {
        lifecycle->leave_requested = 1U;
        if (lifecycle->state == GKD_LIFECYCLE_RECOVERY)
            return 0;
        return advance(lifecycle);
    }
    if (lifecycle->state == GKD_LIFECYCLE_STORAGE) {
        lifecycle->flow = FLOW_STORAGE_ENTER;
        start_leave(lifecycle);
        return advance(lifecycle);
    }
    if (lifecycle->state == GKD_LIFECYCLE_DEBUG) {
        lifecycle->flow = FLOW_DEBUG_ENTER;
        start_leave(lifecycle);
        return advance(lifecycle);
    }
    if (lifecycle->state == GKD_LIFECYCLE_MENU)
        return start_flow(lifecycle, FLOW_CANCEL_MENU);
    if (lifecycle->state == GKD_LIFECYCLE_SUSPENDED)
        return start_flow(lifecycle, FLOW_POWER_RESUME);
    return reject(EALREADY);
}

static int app_ready_event(struct gkd_app_lifecycle *lifecycle,
    uint64_t generation)
{
    if (generation != lifecycle->generation)
        return reject(ESTALE);
    if (!lifecycle->waiting_ready || !lifecycle->app_running)
        return reject(EALREADY);
    lifecycle->app_ready = 1U;
    lifecycle->waiting_ready = 0U;
    return advance(lifecycle);
}

static int hold_app_exit(struct gkd_app_lifecycle *lifecycle)
{
    if (lifecycle->waiting_ready) {
        lifecycle->waiting_ready = 0U;
        if (lifecycle->flow == FLOW_DEBUG_LEAVE)
            lifecycle->step = 3U;
        else if (lifecycle->flow == FLOW_APP_RESTART)
            lifecycle->step = 1U;
        else if (lifecycle->flow == FLOW_CARD_REFRESH)
            lifecycle->step = 4U;
    } else if (lifecycle->flow == FLOW_STORAGE_ENTER) {
        lifecycle->leave_requested = 1U;
    } else if (lifecycle->state == GKD_LIFECYCLE_STORAGE) {
        lifecycle->flow = FLOW_STORAGE_LEAVE;
        lifecycle->step = 0U;
    } else if (lifecycle->state == GKD_LIFECYCLE_ACTIVE ||
        lifecycle->state == GKD_LIFECYCLE_MENU ||
        lifecycle->flow == FLOW_OPEN_MENU ||
        lifecycle->flow == FLOW_CANCEL_MENU ||
        lifecycle->flow == FLOW_CHARGE) {
        lifecycle->flow = FLOW_APP_RESTART;
        lifecycle->step = 0U;
    }
    record_failure(lifecycle, GKD_LIFECYCLE_ACTION_APP_START, ECHILD);
    return 0;
}

static int app_exit_event(struct gkd_app_lifecycle *lifecycle,
    uint64_t generation)
{
    if (generation != lifecycle->generation)
        return reject(ESTALE);
    if (!lifecycle->app_running)
        return reject(EALREADY);
    lifecycle->app_running = 0U;
    lifecycle->app_ready = 0U;
    lifecycle->app_paused = 0U;
    if (lifecycle->pending_action == GKD_LIFECYCLE_ACTION_APP_STOP)
        return 0;
    if ((lifecycle->flow == FLOW_POWER_REBOOT ||
         lifecycle->flow == FLOW_POWER_SHUTDOWN) &&
        lifecycle->pending_action ==
        GKD_LIFECYCLE_ACTION_POWER_QUIESCE)
        return 0;
    if (lifecycle->pending_action != GKD_LIFECYCLE_ACTION_NONE) {
        lifecycle->app_exit_pending = 1U;
        return 0;
    }
    return hold_app_exit(lifecycle);
}

int gkd_app_lifecycle_event(struct gkd_app_lifecycle *lifecycle,
    enum gkd_app_lifecycle_event event, uint64_t generation)
{
    if (!lifecycle || !lifecycle->begin)
        return reject(EINVAL);
    if (event < GKD_LIFECYCLE_EVENT_APP_READY && generation)
        return reject(EINVAL);
    if ((event == GKD_LIFECYCLE_EVENT_APP_READY ||
         event == GKD_LIFECYCLE_EVENT_APP_EXIT) && !generation)
        return reject(EINVAL);
    if (lifecycle->dispatching)
        return reject(EBUSY);
    if (event == GKD_LIFECYCLE_EVENT_APP_READY)
        return app_ready_event(lifecycle, generation);
    if (event == GKD_LIFECYCLE_EVENT_APP_EXIT)
        return app_exit_event(lifecycle, generation);
    if (event == GKD_LIFECYCLE_EVENT_RETURN ||
        event == GKD_LIFECYCLE_EVENT_DETACH)
        return leave_event(lifecycle);
    if(event==GKD_LIFECYCLE_EVENT_CARD_REFRESH){
        if(lifecycle->pending_action!=GKD_LIFECYCLE_ACTION_NONE||lifecycle->usb!=GKD_LIFECYCLE_USB_NONE||
           (lifecycle->state!=GKD_LIFECYCLE_ACTIVE&&!(lifecycle->state==GKD_LIFECYCLE_RECOVERY&&
             lifecycle->failed_action==GKD_LIFECYCLE_ACTION_GAME_MOUNT)))return reject(EBUSY);
        return start_flow(lifecycle,FLOW_CARD_REFRESH);
    }
    if (event == GKD_LIFECYCLE_EVENT_RETRY) {
        enum gkd_app_lifecycle_action failed;
        if (lifecycle->state != GKD_LIFECYCLE_RECOVERY ||
            lifecycle->pending_action != GKD_LIFECYCLE_ACTION_NONE)
            return reject(EINVAL);
        failed = lifecycle->failed_action;
        if (failed == GKD_LIFECYCLE_ACTION_NONE)
            return reject(EINVAL);
        lifecycle->last_error = 0;
        lifecycle->failed_action = GKD_LIFECYCLE_ACTION_NONE;
        if (is_enter_flow(lifecycle->flow) && lifecycle->leave_requested) {
            start_leave(lifecycle);
            return advance(lifecycle);
        }
        if (failed == GKD_LIFECYCLE_ACTION_GAME_UNMOUNT ||
            failed == GKD_LIFECYCLE_ACTION_GAME_MOUNT ||
            failed == GKD_LIFECYCLE_ACTION_GAME_PROBE) {
            if (failed != GKD_LIFECYCLE_ACTION_GAME_PROBE)
                lifecycle->probe_origin = failed;
            return emit(lifecycle, GKD_LIFECYCLE_ACTION_GAME_PROBE);
        }
        return advance(lifecycle);
    }
    if (lifecycle->pending_action != GKD_LIFECYCLE_ACTION_NONE ||
        lifecycle->state == GKD_LIFECYCLE_RECOVERY ||
        lifecycle->state == GKD_LIFECYCLE_TERMINAL)
        return reject(EBUSY);

    switch (event) {
    case GKD_LIFECYCLE_EVENT_MENU:
        if (lifecycle->state != GKD_LIFECYCLE_ACTIVE)
            return reject(EALREADY);
        return start_flow(lifecycle, FLOW_OPEN_MENU);
    case GKD_LIFECYCLE_EVENT_CANCEL:
        if (lifecycle->state != GKD_LIFECYCLE_MENU)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_CANCEL_MENU);
    case GKD_LIFECYCLE_EVENT_CHARGE:
        if (lifecycle->state != GKD_LIFECYCLE_MENU)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_CHARGE);
    case GKD_LIFECYCLE_EVENT_STORAGE:
        if (lifecycle->state != GKD_LIFECYCLE_MENU)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_STORAGE_ENTER);
    case GKD_LIFECYCLE_EVENT_DEBUG:
        if (lifecycle->state != GKD_LIFECYCLE_MENU)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_DEBUG_ENTER);
    case GKD_LIFECYCLE_EVENT_SUSPEND:
        if (lifecycle->state != GKD_LIFECYCLE_MENU &&
            lifecycle->state != GKD_LIFECYCLE_ACTIVE)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_POWER_SUSPEND);
    case GKD_LIFECYCLE_EVENT_REBOOT:
        if (lifecycle->state != GKD_LIFECYCLE_MENU &&
            lifecycle->state != GKD_LIFECYCLE_ACTIVE)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_POWER_REBOOT);
    case GKD_LIFECYCLE_EVENT_SHUTDOWN:
        if (lifecycle->state != GKD_LIFECYCLE_MENU)
            return reject(EINVAL);
        return start_flow(lifecycle, FLOW_POWER_SHUTDOWN);
    default:
        return reject(EINVAL);
    }
}

static int validate_pending(struct gkd_app_lifecycle *lifecycle,
    uint64_t token)
{
    if (!lifecycle || !lifecycle->begin)
        return reject(EINVAL);
    if (lifecycle->dispatching)
        return reject(EBUSY);
    if (lifecycle->pending_action == GKD_LIFECYCLE_ACTION_NONE ||
        token != lifecycle->pending_token)
        return reject(ESTALE);
    return 0;
}

int gkd_app_lifecycle_complete(struct gkd_app_lifecycle *lifecycle,
    uint64_t token)
{
    enum gkd_app_lifecycle_action action;

    if (validate_pending(lifecycle, token))
        return -1;
    action = lifecycle->pending_action;
    if (action == GKD_LIFECYCLE_ACTION_GAME_PROBE)
        return reject(EPROTO);
    lifecycle->pending_action = GKD_LIFECYCLE_ACTION_NONE;
    apply_success(lifecycle, action);
    lifecycle->step++;
    if (lifecycle->app_exit_pending) {
        lifecycle->app_exit_pending = 0U;
        return hold_app_exit(lifecycle);
    }
    return advance(lifecycle);
}

int gkd_app_lifecycle_fail(struct gkd_app_lifecycle *lifecycle,
    uint64_t token, int error)
{
    enum gkd_app_lifecycle_action action;

    if (validate_pending(lifecycle, token))
        return -1;
    if (error <= 0)
        return reject(EINVAL);
    action = lifecycle->pending_action;
    if (action == GKD_LIFECYCLE_ACTION_GAME_UNMOUNT ||
        action == GKD_LIFECYCLE_ACTION_GAME_MOUNT)
        lifecycle->game_media = GKD_LIFECYCLE_MEDIA_UNKNOWN;
    if (action == GKD_LIFECYCLE_ACTION_USB_EXPORT_GAME ||
        action == GKD_LIFECYCLE_ACTION_USB_EXPORT_SYSTEM)
        lifecycle->usb = GKD_LIFECYCLE_USB_UNKNOWN;
    record_failure(lifecycle, action, error);
    return 0;
}

int gkd_app_lifecycle_probe_complete(struct gkd_app_lifecycle *lifecycle,
    uint64_t token, enum gkd_app_lifecycle_media_state state)
{
    enum gkd_app_lifecycle_action origin;

    if (validate_pending(lifecycle, token))
        return -1;
    if (lifecycle->pending_action != GKD_LIFECYCLE_ACTION_GAME_PROBE)
        return reject(EPROTO);
    if (state != GKD_LIFECYCLE_MEDIA_MOUNTED &&
        state != GKD_LIFECYCLE_MEDIA_UNMOUNTED)
        return reject(EINVAL);
    lifecycle->pending_action = GKD_LIFECYCLE_ACTION_NONE;
    lifecycle->game_media = state;
    origin = lifecycle->probe_origin;
    lifecycle->probe_origin = GKD_LIFECYCLE_ACTION_NONE;
    if ((origin == GKD_LIFECYCLE_ACTION_GAME_UNMOUNT &&
         state == GKD_LIFECYCLE_MEDIA_UNMOUNTED) ||
        (origin == GKD_LIFECYCLE_ACTION_GAME_MOUNT &&
         state == GKD_LIFECYCLE_MEDIA_MOUNTED))
        lifecycle->step++;
    lifecycle->failed_action = GKD_LIFECYCLE_ACTION_NONE;
    lifecycle->last_error = 0;
    return advance(lifecycle);
}
