/* sim/overrides/disp_driver.c - SDL 后端 disp_driver 实现
 *
 * 原版驱动 ESP32-S3 LCD 硬件（SPI + axs15231b）。
 * PC 模拟器中由 SDL2 接管显示，所有 LCD 操作转为 no-op，
 * 仅保留 disp_driver.h 中声明的 API 签名以供 UI 代码调用。
 *
 * LVGL 显示驱动 + 鼠标 indev 在 sdl_driver.c 中初始化。
 */
#include "disp_driver.h"
#include "esp_log.h"
#include "../sdl/sdl_driver.h"

static const char *TAG = "disp_driver";

/* disp_driver.h 公开的全局变量 */
volatile uint32_t g_fps_frame_count = 0;
lv_obj_t *g_fps_label = NULL;
int g_rot_state = 0;
int g_canvas_w = 640;
int g_canvas_h = 172;

/* lvgl_lock 在单线程 PC 模拟器中为 no-op，永远返回 true */
bool lvgl_lock(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void lvgl_unlock(void)
{
    /* no-op */
}

void disp_driver_init(void)
{
    ESP_LOGI(TAG, "sim disp_driver_init (SDL backend)");
    if (sdl_driver_init() != 0) {
        ESP_LOGE(TAG, "SDL driver init failed");
        return;
    }
}

void disp_driver_update_resolution(void)
{
    /* no-op: SDL 窗口尺寸固定 */
}

int webui_snapshot_fb(void *out, size_t cap)
{
    (void)out; (void)cap;
    return -1;
}

int disp_driver_get_rot_state(void) { return g_rot_state; }
void disp_driver_set_rot_state(int state) { g_rot_state = state; }
int disp_driver_get_canvas_w(void) { return g_canvas_w; }
int disp_driver_get_canvas_h(void) { return g_canvas_h; }

void disp_driver_get_canvas_size(int *w, int *h)
{
    if (w) *w = g_canvas_w;
    if (h) *h = g_canvas_h;
}

void disp_driver_set_canvas_size(int w, int h)
{
    g_canvas_w = w;
    g_canvas_h = h;
}

lv_obj_t *disp_driver_get_fps_label(void) { return g_fps_label; }

void disp_driver_set_fps_label(lv_obj_t *label)
{
    g_fps_label = label;
}

uint32_t disp_driver_get_fps_frames(void) { return g_fps_frame_count; }
void disp_driver_inc_fps_frames(void) { g_fps_frame_count++; }

uint32_t disp_driver_get_and_reset_fps_frames(void)
{
    uint32_t f = g_fps_frame_count;
    g_fps_frame_count = 0;
    return f;
}

void disp_driver_fps_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t frames = disp_driver_get_and_reset_fps_frames();
    if (g_fps_label) {
        static char fps_text[16];
        snprintf(fps_text, sizeof(fps_text), "FPS %u", frames);
        lv_label_set_text(g_fps_label, fps_text);
    }
}
