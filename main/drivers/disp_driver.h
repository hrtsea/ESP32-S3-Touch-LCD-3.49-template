#ifndef DISP_DRIVER_H
#define DISP_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_fps_frame_count;
extern lv_obj_t *g_fps_label;
extern int g_rot_state;
extern int g_canvas_w;
extern int g_canvas_h;

#define fps_frame_count g_fps_frame_count
#define fps_label g_fps_label
#define rot_state g_rot_state
#define canvas_w g_canvas_w
#define canvas_h g_canvas_h

bool lvgl_lock(int timeout_ms);
void lvgl_unlock(void);
void disp_driver_init(void);
void disp_driver_update_resolution(void);
int webui_snapshot_fb(void *out, size_t cap);

int         disp_driver_get_rot_state(void);
void        disp_driver_set_rot_state(int state);
int         disp_driver_get_canvas_w(void);
int         disp_driver_get_canvas_h(void);
void        disp_driver_get_canvas_size(int *w, int *h);
void        disp_driver_set_canvas_size(int w, int h);
lv_obj_t   *disp_driver_get_fps_label(void);
void        disp_driver_set_fps_label(lv_obj_t *label);
uint32_t    disp_driver_get_fps_frames(void);
void        disp_driver_inc_fps_frames(void);
uint32_t    disp_driver_get_and_reset_fps_frames(void);

#ifdef __cplusplus
}
#endif

#endif
