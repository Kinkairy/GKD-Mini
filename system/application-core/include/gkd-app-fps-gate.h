/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GKD_APP_FPS_GATE_H
#define GKD_APP_FPS_GATE_H
struct gkd_app_fps_gate_paths {
    const char *sdl_alias, *sdl_resolved, *sdl_sha256;
    const char *interposer, *interposer_sha256;
};
/* Read-only generic gate. The fd remains owned by the caller and is the exact
 * fd used for eligible execveat. Returns 1 eligible, 0 unsupported, -1 invalid
 * API/system failure. Unsupported never changes launch behavior. */
int gkd_app_fps_gate(int executable_fd, int working_directory_fd,
                     const char *ld_library_path,
                     const struct gkd_app_fps_gate_paths *paths);
#endif
