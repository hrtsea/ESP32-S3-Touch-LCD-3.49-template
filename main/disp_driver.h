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
void disp_driver_update_resolution(void);

#ifdef __cplusplus
}
#endif

#endif
