/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_FPS_COUNTER_H
#define GKD_FPS_COUNTER_H
#include <stdint.h>
/* The pinned 2014 userspace headers predate Linux memfd sealing constants. */
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#endif
#define GKD_FPS_COUNTER_MAGIC UINT32_C(0x47504653) /* GPFS */
#define GKD_FPS_COUNTER_VERSION UINT32_C(1)
#define GKD_FPS_COUNTER_BYTES UINT32_C(64)
#define GKD_FPS_PRODUCER_UNAVAILABLE UINT32_C(0)
#define GKD_FPS_PRODUCER_ATTACHED UINT32_C(1)
#define GKD_FPS_COUNTER_FD_ENV "GKD_FPS_COUNTER_FD"
#define GKD_FPS_LIFETIME_FD_ENV "GKD_FPS_LIFETIME_FD"
#define GKD_FPS_PRELOAD_PATH_ENV "GKD_FPS_PRELOAD_PATH"
#define GKD_FPS_SESSION_ENV "GKD_FPS_SESSION"
struct gkd_fps_counter_page {
    uint32_t magic;
    uint32_t version;
    uint32_t bytes;
    uint32_t flags;
    uint64_t session_hi;
    uint64_t session_lo;
    /* Producer uses atomic RMW; reader uses an atomic relaxed 32-bit load. */
    volatile uint32_t successful_flips;
    /* Constructor release-store; reader acquire-loads before trusting count. */
    volatile uint32_t producer_state;
    uint32_t reserved[6];
};
typedef char gkd_fps_counter_page_must_be_64_bytes[
    sizeof(struct gkd_fps_counter_page) == GKD_FPS_COUNTER_BYTES ? 1 : -1];
static inline uint32_t gkd_fps_counter_load(const struct gkd_fps_counter_page *page)
{
    return __atomic_load_n(&page->successful_flips, __ATOMIC_RELAXED);
}
static inline uint32_t gkd_fps_producer_load(const struct gkd_fps_counter_page *page)
{
    return __atomic_load_n(&page->producer_state, __ATOMIC_ACQUIRE);
}
static inline uint32_t gkd_fps_counter_delta(uint32_t current, uint32_t previous)
{
    return current - previous;
}
#endif
