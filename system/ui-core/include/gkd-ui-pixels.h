/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_UI_PIXELS_H
#define GKD_UI_PIXELS_H

/* Freestanding integer operations shared by the UI renderer and compositor.
 * Callers validate alpha <=255 and provide a clean underlay. */
static inline unsigned short gkd_ui_blend565_value(unsigned short under,
                                                   unsigned short over,
                                                   unsigned alpha)
{
    unsigned ur = (under >> 11) & 31U, ug = (under >> 5) & 63U, ub = under & 31U;
    unsigned or_ = (over >> 11) & 31U, og = (over >> 5) & 63U, ob = over & 31U;
    unsigned r = (ur * (255U - alpha) + or_ * alpha + 127U) / 255U;
    unsigned g = (ug * (255U - alpha) + og * alpha + 127U) / 255U;
    unsigned b = (ub * (255U - alpha) + ob * alpha + 127U) / 255U;
    return (unsigned short)((r << 11) | (g << 5) | b);
}

static inline int gkd_ui_fade_value(unsigned elapsed_ms, unsigned remaining_ms,
                                    unsigned fade_ms, unsigned *opacity)
{
    unsigned in, out;
    if (!opacity || fade_ms > 1000U) return -1;
    if (!remaining_ms) { *opacity = 0U; return 0; }
    if (!fade_ms) { *opacity = 255U; return 0; }
    in = elapsed_ms < fade_ms ? elapsed_ms : fade_ms;
    out = remaining_ms < fade_ms ? remaining_ms : fade_ms;
    in = in * 255U / fade_ms;
    out = out * 255U / fade_ms;
    *opacity = in < out ? in : out;
    return 0;
}
#endif
