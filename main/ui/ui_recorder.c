#include "ui_recorder.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "radio.h"
#include "recorder.h"
#include "i18n.h"
#include "event_bus.h"

static const char *TAG = "ui_recorder";

/* ---------------------- Recorder tile ---------------------- */

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Recorder                = NULL;
lv_obj_t *ui_Recorder_label_status    = NULL;
lv_obj_t *ui_Recorder_label_rec_btn   = NULL;
lv_obj_t *ui_Recorder_bar_vu_l       = NULL;
lv_obj_t *ui_Recorder_bar_vu_r       = NULL;

/* ===== 2. 静态样式变量 ===== */
static lv_style_t style_tile_bg;
static lv_style_t style_list_btn;
static lv_style_t style_rec_btn;
static lv_style_t style_vu_bar;
static lv_style_t style_overlay_bg;
static lv_style_t style_overlay_list;
static lv_style_t style_row;
static lv_style_t style_del_btn;
static lv_style_t style_back_btn;
static bool styles_inited = false;

/* 内部状态变量 */
static lv_obj_t  *g_list_overlay     = NULL;
static lv_obj_t  *g_list             = NULL;
static lv_obj_t  *g_overlay_status   = NULL;
static lv_timer_t *g_poll            = NULL;
static int        g_vu_l_smooth     = 0;
static int        g_vu_r_smooth     = 0;
/* 播放/录音状态标志：由事件总线更新，避免每 100ms 轮询 radio_is_playing() */
static volatile bool g_playback_active = false;
static volatile bool g_recording_active = false;
/* 标志：收到事件后立即触发一次 UI 刷新（无需等 100ms 轮询） */
static volatile bool g_ui_dirty = true;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_make(0x10, 0x10, 0x18));
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_tile_bg, 4);

    lv_style_init(&style_list_btn);
    lv_style_set_bg_color(&style_list_btn, lv_color_make(0x40, 0x40, 0x60));

    lv_style_init(&style_rec_btn);
    lv_style_set_radius(&style_rec_btn, 65);
    lv_style_set_bg_color(&style_rec_btn, lv_color_make(0xc0, 0x30, 0x30));

    lv_style_init(&style_vu_bar);
    lv_style_set_bg_color(&style_vu_bar, lv_color_make(0x30, 0x30, 0x30));
    lv_style_set_radius(&style_vu_bar, 3);

    lv_style_init(&style_overlay_bg);
    lv_style_set_bg_color(&style_overlay_bg, lv_color_make(0x10, 0x10, 0x18));
    lv_style_set_bg_opa(&style_overlay_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_overlay_bg, 4);

    lv_style_init(&style_overlay_list);
    lv_style_set_bg_opa(&style_overlay_list, LV_OPA_TRANSP);
    lv_style_set_pad_row(&style_overlay_list, 2);
    lv_style_set_pad_all(&style_overlay_list, 4);

    lv_style_init(&style_row);
    lv_style_set_bg_color(&style_row, lv_color_make(0x18, 0x40, 0x28));
    lv_style_set_pad_all(&style_row, 6);

    lv_style_init(&style_del_btn);
    lv_style_set_bg_color(&style_del_btn, lv_color_make(0x80, 0x40, 0x20));

    lv_style_init(&style_back_btn);
    lv_style_set_bg_color(&style_back_btn, lv_color_make(0x40, 0x40, 0x60));

    styles_inited = true;
}

/* ===== 业务辅助函数 ===== */

static int peak_to_pct(uint16_t peak)
{
    if (peak == 0) return 0;
    int log2v = 0;
    uint32_t v = peak;
    while (v >>= 1) log2v++;
    int t = (log2v * 100) / 15;
    return t > 100 ? 100 : t;
}

/* ===== 3. 事件回调函数 ===== */

void ui_event_Recorder_item_play(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name || !*name) return;
    char path[128];
    snprintf(path, sizeof(path), "file://sdcard/recordings/%s", name);
    if (radio_is_playing()) radio_stop();
    radio_play(path);
}

void ui_event_Recorder_item_delete(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    recorder_delete(name);
    recorder_refresh_list();
}

void ui_event_Recorder_btn_rec(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "rec_btn tap: blocked=%d recording=%d",
             (int)menu_input_blocked(), (int)recorder_is_recording());
    if (menu_input_blocked()) return;
    if (recorder_is_recording()) {
        esp_err_t r = recorder_stop();
        ESP_LOGI(TAG, "rec_btn stop -> %s", esp_err_to_name(r));
        recorder_refresh_list();
    } else {
        const char *path = NULL;
        esp_err_t r = recorder_start(&path);
        ESP_LOGI(TAG, "rec_btn start -> %s path=%s",
                 esp_err_to_name(r), path ? path : "(null)");
    }
}

void ui_event_Recorder_btn_list_close(lv_event_t *e)
{
    (void)e;
    if (g_list_overlay) {
        lv_obj_del(g_list_overlay);
        g_list_overlay = NULL;
        g_list = NULL;
        g_overlay_status = NULL;
    }
}

void ui_event_Recorder_btn_list_open(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    if (g_list_overlay) return;
    /* Mount the overlay on the active screen's TOP LAYER so the
       tileview can't capture our taps as start-of-swipe gestures. */
    g_list_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_list_overlay);
    lv_obj_set_size(g_list_overlay, disp_driver_get_canvas_w(), disp_driver_get_canvas_h());
    lv_obj_align(g_list_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_style(g_list_overlay, &style_overlay_bg, 0);
    lv_obj_clear_flag(g_list_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(g_list_overlay);
    lv_obj_set_size(back, 56, 22);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_style(back, &style_back_btn, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, ui_event_Recorder_btn_list_close, LV_EVENT_CLICKED, NULL);

    /* Now-playing banner pinned to the right of the Back button. */
    g_overlay_status = lv_label_create(g_list_overlay);
    lv_label_set_text(g_overlay_status, "");
    lv_obj_set_style_text_font(g_overlay_status, &lv_font_montserrat_16, 0);
    lv_obj_align(g_overlay_status, LV_ALIGN_TOP_LEFT, 64, 2);
    lv_label_set_long_mode(g_overlay_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(g_overlay_status, disp_driver_get_canvas_w() - 70);

    g_list = lv_obj_create(g_list_overlay);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, lv_pct(100), lv_pct(85));
    lv_obj_align(g_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(g_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(g_list, &style_overlay_list, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_AUTO);
    recorder_refresh_list();
}

/* ===== 业务 API（list 刷新） ===== */

void recorder_refresh_list(void)
{
    if (!g_list) return;
    lv_obj_clean(g_list);
    static char names[16][64];
    int n = recorder_list(names, 16);
    if (n == 0) {
        lv_obj_t *l = lv_label_create(g_list);
        lv_label_set_text(l, "(no recordings)");
        lv_obj_set_style_text_color(l, lv_color_make(0xa0, 0xa0, 0xa0), 0);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_btn_create(g_list);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_add_style(row, &style_row, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, ui_event_Recorder_item_play, LV_EVENT_CLICKED,
                            (void *)names[i]);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, names[i]);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *meta = lv_label_create(row);
        uint32_t bytes = 0, dur_ms = 0;
        recorder_file_info(names[i], &bytes, &dur_ms);
        char meta_buf[48];
        if (bytes >= 1024 * 1024) {
            snprintf(meta_buf, sizeof(meta_buf), "%.1f MB  %u.%01us",
                     bytes / (1024.0 * 1024.0),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        } else {
            snprintf(meta_buf, sizeof(meta_buf), "%u KB  %u.%01us",
                     (unsigned)(bytes / 1024),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        }
        lv_label_set_text(meta, meta_buf);
        lv_obj_set_style_text_color(meta, lv_color_make(0xc0, 0xc0, 0xc0), 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 40, 32);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_style(del, &style_del_btn, 0);
        lv_obj_t *dl = lv_label_create(del);
        lv_label_set_text(dl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(dl, lv_color_white(), 0);
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
        lv_obj_center(dl);
        lv_obj_add_event_cb(del, ui_event_Recorder_item_delete, LV_EVENT_CLICKED,
                            (void *)names[i]);
    }
}

/* ===== 定时器回调 ===== */

static void rec_poll_cb(lv_timer_t *t)
{
    (void)t;
    bool recording = g_recording_active || recorder_is_recording();
    bool playing   = g_playback_active || radio_is_playing();
    if (ui_Recorder_label_rec_btn) {
        lv_label_set_text(ui_Recorder_label_rec_btn, recording ? LV_SYMBOL_STOP : "REC");
    }
    if (ui_Recorder_label_status) {
        if (recording) {
            lv_label_set_text_fmt(ui_Recorder_label_status, LV_SYMBOL_AUDIO " REC  %us",
                                  recorder_elapsed_s());
            lv_obj_set_style_text_color(ui_Recorder_label_status, lv_color_make(0xff, 0x40, 0x40), 0);
        } else if (playing) {
            const char *uri = radio_current_uri();
            const char *base = uri ? uri : "stream";
            const char *slash = NULL;
            if (uri) {
                for (const char *p = uri; *p; p++) if (*p == '/') slash = p;
                if (slash) base = slash + 1;
            }
            lv_label_set_text_fmt(ui_Recorder_label_status, LV_SYMBOL_PLAY " Playing %s", base);
            lv_obj_set_style_text_color(ui_Recorder_label_status, lv_color_make(0x40, 0xc0, 0xff), 0);
            if (g_overlay_status) {
                lv_label_set_text_fmt(g_overlay_status,
                                      LV_SYMBOL_PLAY " Playing %s", base);
                lv_obj_set_style_text_color(g_overlay_status,
                                            lv_color_make(0x40, 0xc0, 0xff), 0);
            }
        } else {
            lv_label_set_text(ui_Recorder_label_status, "Idle");
            lv_obj_set_style_text_color(ui_Recorder_label_status, lv_color_white(), 0);
            if (g_overlay_status) {
                lv_label_set_text(g_overlay_status, "");
            }
        }
    }
    uint16_t mic_l = 0, mic_r = 0;
    uint16_t out_l = 0, out_r = 0;
    recorder_peak_lr(&mic_l, &mic_r);
    radio_out_peak(&out_l, &out_r);
    uint16_t pl = playing ? out_l : mic_l;
    uint16_t pr = playing ? out_r : mic_r;
    int tl = peak_to_pct(pl);
    int tr = peak_to_pct(pr);
    if (tl > g_vu_l_smooth) g_vu_l_smooth = tl;
    else g_vu_l_smooth = (g_vu_l_smooth * 7 + tl) / 8;
    if (tr > g_vu_r_smooth) g_vu_r_smooth = tr;
    else g_vu_r_smooth = (g_vu_r_smooth * 7 + tr) / 8;
    if (ui_Recorder_bar_vu_l) {
        lv_obj_set_style_bg_color(ui_Recorder_bar_vu_l,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(ui_Recorder_bar_vu_l, g_vu_l_smooth, LV_ANIM_OFF);
    }
    if (ui_Recorder_bar_vu_r) {
        lv_obj_set_style_bg_color(ui_Recorder_bar_vu_r,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(ui_Recorder_bar_vu_r, g_vu_r_smooth, LV_ANIM_OFF);
    }
    g_ui_dirty = false;
}

/* ===== 事件总线 handler ===== */

static void on_play_start_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    g_playback_active = true;
    g_ui_dirty = true;
}

static void on_play_stop_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    g_playback_active = false;
    g_ui_dirty = true;
}

static void on_record_start_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    g_recording_active = true;
    g_ui_dirty = true;
}

static void on_record_stop_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;
    g_recording_active = false;
    g_ui_dirty = true;
}

/* ===== tile 进入/离开 ===== */

void recorder_tile_on_enter(void)
{
    if (g_poll) {
        lv_timer_resume(g_poll);
    }
    if (!recorder_is_recording()) {
        recorder_monitor_start();
    }
}

void recorder_tile_on_leave(void)
{
    if (!recorder_is_recording()) {
        recorder_monitor_stop();
    }
    if (g_poll) {
        lv_timer_pause(g_poll);
    }
}

/* ===== 公共 UI API ===== */

void ui_Recorder_cleanup(void)
{
    /* 取消事件订阅 */
    event_bus_unsubscribe(EVENT_AUDIO_PLAY_START,   on_play_start_evt);
    event_bus_unsubscribe(EVENT_AUDIO_PLAY_STOP,    on_play_stop_evt);
    event_bus_unsubscribe(EVENT_AUDIO_RECORD_START, on_record_start_evt);
    event_bus_unsubscribe(EVENT_AUDIO_RECORD_STOP,  on_record_stop_evt);

    if (g_poll) { lv_timer_del(g_poll); g_poll = NULL; }
    ui_Recorder                = NULL;
    ui_Recorder_label_status    = NULL;
    ui_Recorder_label_rec_btn   = NULL;
    ui_Recorder_bar_vu_l       = NULL;
    ui_Recorder_bar_vu_r       = NULL;
    /* overlay 由 lv_layer_top() 持有，旋转时由 LVGL 自行销毁；这里只置空指针 */
    g_list_overlay              = NULL;
    g_list                      = NULL;
    g_overlay_status            = NULL;
}

/* ===== 4. tile 创建函数 ===== */

void ui_Recorder_create(lv_obj_t *parent)
{
    init_styles();

    ui_Recorder = parent;
    lv_obj_clear_flag(ui_Recorder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Recorder, &style_tile_bg, 0);

    /* Left column: status + recordings button. */
    ui_Recorder_label_status = lv_label_create(parent);
    lv_label_set_text(ui_Recorder_label_status, "Idle");
    lv_obj_set_style_text_color(ui_Recorder_label_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_Recorder_label_status, i18n_font(), 0);
    lv_obj_align(ui_Recorder_label_status, LV_ALIGN_LEFT_MID, 12, -32);

    lv_obj_t *list_btn = lv_btn_create(parent);
    lv_obj_set_size(list_btn, 150, 36);
    lv_obj_align(list_btn, LV_ALIGN_LEFT_MID, 12, 24);
    lv_obj_add_style(list_btn, &style_list_btn, 0);
    lv_obj_t *lbl = lv_label_create(list_btn);
    lv_label_set_text(lbl, "Recordings " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, i18n_font(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(list_btn, ui_event_Recorder_btn_list_open, LV_EVENT_CLICKED, NULL);

    /* Center: big round REC button. */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 130, 130);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(btn, &style_rec_btn, 0);
    lv_obj_add_event_cb(btn, ui_event_Recorder_btn_rec, LV_EVENT_CLICKED, NULL);
    ui_Recorder_label_rec_btn = lv_label_create(btn);
    lv_label_set_text(ui_Recorder_label_rec_btn, "REC");
    lv_obj_set_style_text_color(ui_Recorder_label_rec_btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_Recorder_label_rec_btn, &lv_font_montserrat_48, 0);
    lv_obj_center(ui_Recorder_label_rec_btn);

    /* Right side: stereo L/R VU stacked vertically. */
    lv_obj_t *l_lbl = lv_label_create(parent);
    lv_label_set_text(l_lbl, "L");
    lv_obj_set_style_text_color(l_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(l_lbl, i18n_font(), 0);
    lv_obj_align(l_lbl, LV_ALIGN_RIGHT_MID, -150, -22);

    ui_Recorder_bar_vu_l = lv_bar_create(parent);
    lv_obj_set_size(ui_Recorder_bar_vu_l, 130, 14);
    lv_obj_align(ui_Recorder_bar_vu_l, LV_ALIGN_RIGHT_MID, -8, -22);
    lv_bar_set_range(ui_Recorder_bar_vu_l, 0, 100);
    lv_obj_add_style(ui_Recorder_bar_vu_l, &style_vu_bar, 0);
    lv_obj_set_style_bg_color(ui_Recorder_bar_vu_l, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui_Recorder_bar_vu_l, 3, LV_PART_INDICATOR);

    lv_obj_t *r_lbl = lv_label_create(parent);
    lv_label_set_text(r_lbl, "R");
    lv_obj_set_style_text_color(r_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(r_lbl, i18n_font(), 0);
    lv_obj_align(r_lbl, LV_ALIGN_RIGHT_MID, -150, 22);

    ui_Recorder_bar_vu_r = lv_bar_create(parent);
    lv_obj_set_size(ui_Recorder_bar_vu_r, 130, 14);
    lv_obj_align(ui_Recorder_bar_vu_r, LV_ALIGN_RIGHT_MID, -8, 22);
    lv_bar_set_range(ui_Recorder_bar_vu_r, 0, 100);
    lv_obj_add_style(ui_Recorder_bar_vu_r, &style_vu_bar, 0);
    lv_obj_set_style_bg_color(ui_Recorder_bar_vu_r, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui_Recorder_bar_vu_r, 3, LV_PART_INDICATOR);

    if (!g_poll) {
        g_poll = lv_timer_create(rec_poll_cb, 100, NULL);
        lv_timer_pause(g_poll);
    }

    /* 订阅事件总线：播放/录音状态变化时立即刷新 UI（无需等 100ms 轮询） */
    event_bus_subscribe(EVENT_AUDIO_PLAY_START,   on_play_start_evt,   NULL);
    event_bus_subscribe(EVENT_AUDIO_PLAY_STOP,    on_play_stop_evt,    NULL);
    event_bus_subscribe(EVENT_AUDIO_RECORD_START, on_record_start_evt, NULL);
    event_bus_subscribe(EVENT_AUDIO_RECORD_STOP,  on_record_stop_evt,  NULL);
}

/* ===== 5. tile 清理函数 ===== */
/* (已在上方定义 ui_Recorder_cleanup) */
