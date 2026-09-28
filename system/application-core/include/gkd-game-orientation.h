/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_GAME_ORIENTATION_H
#define GKD_GAME_ORIENTATION_H
#include <stdint.h>
#include <stdio.h>
#define GKD_ORIENTATION_VERSION 1U
#define GKD_ORIENTATION_UNKNOWN UINT32_MAX
#define GKD_ORIENTATION_FD_ENV "GKD_ORIENTATION_FD"
#define GKD_ORIENTATION_MAGIC UINT32_C(0x474f5231)
enum { GKD_ASPECT_UNKNOWN, GKD_ASPECT_LANDSCAPE, GKD_ASPECT_PORTRAIT };
enum { GKD_HINT_UNKNOWN, GKD_HINT_NONE, GKD_HINT_LEFT, GKD_HINT_RIGHT };
enum { GKD_ORIGIN_UNKNOWN, GKD_ORIGIN_LNX_HEADER, GKD_ORIGIN_LYNX_CRC,
       GKD_ORIGIN_WS_FOOTER, GKD_ORIGIN_BURN_DRIVER };
enum { GKD_SCOPE_UNKNOWN, GKD_SCOPE_LAUNCH_ROM, GKD_SCOPE_ACTIVE_DRIVER };
/* Rotation hints describe cartridge metadata, never the current presentation.
 * Display/input rotations require an explicit runtime adapter; unknown is not 0. */
struct gkd_game_orientation {
    uint32_t version, aspect, hint, display_rotation, input_rotation, source, scope, reserved;
};
#define GKD_GAME_ORIENTATION_INIT {GKD_ORIENTATION_VERSION,0,0,GKD_ORIENTATION_UNKNOWN,GKD_ORIENTATION_UNKNOWN,0,0,0}
struct gkd_orientation_page {
    uint32_t magic, version;
    volatile uint32_t aspect;
    uint32_t reserved;
    char driver[64];
};
typedef char gkd_orientation_wire_size[(sizeof(struct gkd_game_orientation)==32)?1:-1];
typedef char gkd_orientation_page_size[(sizeof(struct gkd_orientation_page)==80)?1:-1];
static inline int gkd_game_orientation_valid(const struct gkd_game_orientation *s)
{
    if(!s||s->version!=1||s->aspect>2||s->hint>3||s->source>4||s->scope>2||s->reserved)return 0;
    if(s->display_rotation!=GKD_ORIENTATION_UNKNOWN||s->input_rotation!=GKD_ORIENTATION_UNKNOWN)return 0;
    if(!s->aspect)return !s->hint&&!s->source&&!s->scope;
    return s->source==GKD_ORIGIN_BURN_DRIVER?s->scope==GKD_SCOPE_ACTIVE_DRIVER&&!s->hint:
        s->source>0&&s->source<4&&s->scope==GKD_SCOPE_LAUNCH_ROM;
}
static inline void gkd_game_orientation_print(const struct gkd_game_orientation *s,int active)
{
    static const char *const aspects[]={"unknown","landscape","portrait"};
    static const char *const hints[]={"unknown","none","left","right"};
    static const char *const sources[]={"unknown","lnx-header","lynx-crc","ws-footer","burn-driver"};
    static const char *const scopes[]={"unknown","launch-rom","active-driver"};
    printf("GKD_GAME_ORIENTATION=1 session=%s aspect=%s hint=%s display=unknown input=unknown source=%s scope=%s\n",
        active?"active":"idle",aspects[s->aspect],hints[s->hint],sources[s->source],scopes[s->scope]);
}
#endif
