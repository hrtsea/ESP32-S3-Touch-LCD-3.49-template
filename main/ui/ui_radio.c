#include "ui_radio.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "radio.h"
#include "recorder.h"
#include "i18n.h"
#include "event_bus.h"

/* ---------------------- Radio tile ---------------------- */

static const char *TAG = "ui_radio";

/* ===== 1. 对象定义 ===== */
lv_obj_t *ui_Radio                = NULL;
lv_obj_t *ui_Radio_label_status    = NULL;
lv_obj_t *ui_Radio_label_now       = NULL;
lv_obj_t *ui_Radio_label_play_btn  = NULL;
lv_obj_t *ui_Radio_list           = NULL;
lv_obj_t *ui_Radio_label_vol      = NULL;

/* ===== 2. 静态样式变量 ===== */
static lv_style_t style_tile_bg;
static lv_style_t style_list;
static lv_style_t style_station_row;
static lv_style_t style_info;
static lv_style_t style_header;
static lv_style_t style_now;
static lv_style_t style_status;
static lv_style_t style_vol_btn;
static lv_style_t style_play_btn;
static bool styles_inited = false;

/* 内部状态变量 */
static bool       g_engine_up      = false;
static lv_timer_t *g_poll_timer    = NULL;
static int        g_pending_idx    = -1;

typedef enum {
    RADIO_CMD_NONE = 0,
    RADIO_CMD_PLAY_INDEX,
    RADIO_CMD_STOP,
    RADIO_CMD_INIT_ENGINE,
} radio_cmd_t;

static volatile radio_cmd_t  g_cmd        = RADIO_CMD_NONE;
static char                  g_status[80] = "Idle";
static SemaphoreHandle_t     g_status_mtx = NULL;
static TaskHandle_t          g_worker     = NULL;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_tile_bg);
    lv_style_set_bg_color(&style_tile_bg, lv_color_make(0x10, 0x10, 0x18));
    lv_style_set_bg_opa(&style_tile_bg, LV_OPA_COVER);
    lv_style_set_pad_all(&style_tile_bg, 0);

    lv_style_init(&style_list);
    lv_style_set_bg_color(&style_list, lv_color_make(0x10, 0x10, 0x18));
    lv_style_set_bg_opa(&style_list, LV_OPA_COVER);
    lv_style_set_pad_all(&style_list, 4);
    lv_style_set_pad_row(&style_list, 2);

    lv_style_init(&style_station_row);
    lv_style_set_bg_color(&style_station_row, lv_color_make(0x20, 0x20, 0x30));
    lv_style_set_pad_all(&style_station_row, 4);

    lv_style_init(&style_info);
    lv_style_set_bg_color(&style_info, lv_color_make(0x18, 0x18, 0x24));
    lv_style_set_bg_opa(&style_info, LV_OPA_COVER);
    lv_style_set_pad_all(&style_info, 6);

    lv_style_init(&style_header);
    lv_style_set_text_color(&style_header, lv_color_make(0xa0, 0xa0, 0xc0));

    lv_style_init(&style_now);
    lv_style_set_text_color(&style_now, lv_color_white());

    lv_style_init(&style_status);
    lv_style_set_text_color(&style_status, lv_color_make(0xc0, 0xc0, 0xc0));

    lv_style_init(&style_vol_btn);
    lv_style_set_radius(&style_vol_btn, 4);
    lv_style_set_bg_color(&style_vol_btn, lv_color_make(0x40, 0x40, 0x60));

    lv_style_init(&style_play_btn);
    lv_style_set_radius(&style_play_btn, 4);
    lv_style_set_bg_color(&style_play_btn, lv_color_make(0x20, 0x80, 0x40));

    styles_inited = true;
}

/* ===== 业务辅助函数 ===== */

static void radio_set_status(const char *s)
{
    if (!g_status_mtx) return;
    xSemaphoreTake(g_status_mtx, portMAX_DELAY);
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = 0;
    xSemaphoreGive(g_status_mtx);
}

static bool radio_engine_ensure_up(void)
{
    if (g_engine_up) return true;
    if (radio_get_play_dev() != NULL) {
        g_engine_up = true;
        return true;
    }
    radio_set_status(tr(I18N_RADIO_ENGINE_INIT));
    if (radio_init() != ESP_OK) {
        radio_set_status(tr(I18N_RADIO_ENGINE_FAIL));
        return false;
    }
    g_engine_up = true;
    return true;
}

static void radio_worker_task(void *arg)
{
    (void)arg;
    while (1) {
        radio_cmd_t cmd = g_cmd;
        if (cmd == RADIO_CMD_INIT_ENGINE) {
            g_cmd = RADIO_CMD_NONE;
            radio_engine_ensure_up();
        } else if (cmd == RADIO_CMD_PLAY_INDEX) {
            g_cmd = RADIO_CMD_NONE;
            int idx = g_pending_idx;
            if (!radio_engine_ensure_up()) continue;
            char buf[80];
            snprintf(buf, sizeof(buf), tr(I18N_RADIO_CONNECTING), radio_station_name(idx));
            radio_set_status(buf);
            if (radio_play_index(idx) == ESP_OK) {
                snprintf(buf, sizeof(buf), tr(I18N_RADIO_PLAYING), radio_station_name(idx));
                radio_set_status(buf);
            } else {
                radio_set_status(tr(I18N_RADIO_PLAY_FAIL));
            }
        } else if (cmd == RADIO_CMD_STOP) {
            g_cmd = RADIO_CMD_NONE;
            if (g_engine_up) radio_stop();
            radio_set_status(tr(I18N_RADIO_STOPPED));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void radio_worker_ensure(void)
{
    if (!g_status_mtx) g_status_mtx = xSemaphoreCreateMutex();
    if (!g_worker) {
        xTaskCreatePinnedToCore(radio_worker_task, "radio_wrk", 6 * 1024,
                                NULL, 4, &g_worker, 1);
    }
}

void radio_engine_warm_at_boot(void)
{
    radio_worker_ensure();
    g_cmd = RADIO_CMD_INIT_ENGINE;
}

static void radio_status_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_Radio_label_status || !g_status_mtx) return;
    char snap[80];
    xSemaphoreTake(g_status_mtx, portMAX_DELAY);
    strncpy(snap, g_status, sizeof(snap));
    xSemaphoreGive(g_status_mtx);
    lv_label_set_text(ui_Radio_label_status, snap);
    if (ui_Radio_label_play_btn) {
        bool playing = g_engine_up && radio_is_playing();
        lv_label_set_text(ui_Radio_label_play_btn, playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    if (ui_Radio_label_now) {
        int idx = radio_current_index();
        if (idx >= 0) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%s  -  %s",
                     radio_station_name(idx), radio_station_genre(idx));
            lv_label_set_text(ui_Radio_label_now, buf);
        }
    }
}

static void radio_vol_step(int delta)
{
    int v = radio_get_volume() + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    radio_set_volume(v);
    if (ui_Radio_label_vol)
        lv_label_set_text_fmt(ui_Radio_label_vol, tr(I18N_VOL_N), v);

    /* 发布音量变更事件，供其他模块（如 ui_settings）同步 UI */
    uint8_t vol = (uint8_t)v;
    event_bus_publish(EVENT_AUDIO_VOLUME_CHANGED, &vol, sizeof(vol));
}

/* ===== 3. 事件回调函数 ===== */

void ui_event_Radio_btn_vol_dn(lv_event_t *e)
{
    (void)e;
    radio_vol_step(-5);
}

void ui_event_Radio_btn_vol_up(lv_event_t *e)
{
    (void)e;
    radio_vol_step(+5);
}

void ui_event_Radio_btn_play(lv_event_t *e)
{
    (void)e;
    radio_worker_ensure();
    if (g_engine_up && radio_is_playing()) {
        g_cmd = RADIO_CMD_STOP;
    } else {
        int idx = radio_current_index();
        g_pending_idx = idx >= 0 ? idx : 0;
        g_cmd = RADIO_CMD_PLAY_INDEX;
    }
}

void ui_event_Radio_station_pick(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= radio_station_count()) return;
    radio_worker_ensure();
    g_pending_idx = idx;
    g_cmd = RADIO_CMD_PLAY_INDEX;
}

/* ===== 事件总线 handler ===== */

/* 监听 EVENT_AUDIO_VOLUME_CHANGED，当其他模块（如 ui_settings）改变音量时，
   同步刷新本 tile 的 "Vol N" 标签。 */
static void on_audio_volume_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!ui_Radio_label_vol || !evt || !evt->data) return;
    uint8_t v = *(const uint8_t *)evt->data;
    lv_label_set_text_fmt(ui_Radio_label_vol, tr(I18N_VOL_N), (int)v);
}

/* ===== 公共 UI API ===== */

void ui_Radio_cleanup(void)
{
    /* 取消事件订阅 */
    event_bus_unsubscribe(EVENT_AUDIO_VOLUME_CHANGED, on_audio_volume_changed_evt);

    if (g_poll_timer) { lv_timer_del(g_poll_timer); g_poll_timer = NULL; }
    ui_Radio                = NULL;
    ui_Radio_label_status    = NULL;
    ui_Radio_label_now       = NULL;
    ui_Radio_label_play_btn  = NULL;
    ui_Radio_list           = NULL;
    ui_Radio_label_vol      = NULL;
}

/* ===== 4. tile 创建函数 ===== */

void ui_Radio_create(lv_obj_t *parent)
{
    init_styles();

    /* tile 容器 */
    ui_Radio = parent;
    lv_obj_clear_flag(ui_Radio, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Radio, &style_tile_bg, 0);

    /* Split horizontally: scrollable station list on the left, now-playing
       info + transport on the right. The 640 px canvas gives ~62/38 split. */
    const int LIST_W = 400;
    const int INFO_W = disp_driver_get_canvas_w() - LIST_W;

    /* ---------- Left: station list ---------- */
    ui_Radio_list = lv_obj_create(ui_Radio);
    lv_obj_remove_style_all(ui_Radio_list);
    lv_obj_set_size(ui_Radio_list, LIST_W, disp_driver_get_canvas_h());
    lv_obj_align(ui_Radio_list, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_layout(ui_Radio_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_Radio_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(ui_Radio_list, &style_list, 0);
    lv_obj_set_scroll_dir(ui_Radio_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_Radio_list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < radio_station_count(); i++) {
        lv_obj_t *row = lv_btn_create(ui_Radio_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 26);
        lv_obj_add_style(row, &style_station_row, 0);
        lv_obj_add_event_cb(row, ui_event_Radio_station_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s  -  %s",
                              radio_station_name(i),
                              radio_station_genre(i));
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, i18n_font(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    /* ---------- Right: info panel ---------- */
    lv_obj_t *info = lv_obj_create(ui_Radio);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, INFO_W, disp_driver_get_canvas_h());
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_style(info, &style_info, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(info);
    lv_label_set_text_fmt(header, LV_SYMBOL_AUDIO "  %s", tr(I18N_RADIO_NOW_PLAYING));
    lv_obj_add_style(header, &style_header, 0);
    lv_obj_set_style_text_font(header, i18n_font(), 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    ui_Radio_label_now = lv_label_create(info);
    lv_label_set_long_mode(ui_Radio_label_now, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_Radio_label_now, INFO_W - 12);
    lv_label_set_text(ui_Radio_label_now, tr(I18N_RADIO_NO_STATION));
    lv_obj_add_style(ui_Radio_label_now, &style_now, 0);
    lv_obj_set_style_text_font(ui_Radio_label_now, i18n_font(), 0);
    lv_obj_align(ui_Radio_label_now, LV_ALIGN_TOP_LEFT, 0, 18);

    ui_Radio_label_status = lv_label_create(info);
    lv_label_set_long_mode(ui_Radio_label_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_Radio_label_status, INFO_W - 12);
    lv_label_set_text(ui_Radio_label_status, tr(I18N_IDLE));
    lv_obj_add_style(ui_Radio_label_status, &style_status, 0);
    lv_obj_set_style_text_font(ui_Radio_label_status, i18n_font(), 0);
    lv_obj_align(ui_Radio_label_status, LV_ALIGN_TOP_LEFT, 0, 70);

    /* Bottom row: [-]  Vol N  [+]                             [play] */
    lv_obj_t *vol_dn = lv_btn_create(info);
    lv_obj_set_size(vol_dn, 32, 32);
    lv_obj_align(vol_dn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_style(vol_dn, &style_vol_btn, 0);
    lv_obj_add_event_cb(vol_dn, ui_event_Radio_btn_vol_dn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_dn_l = lv_label_create(vol_dn);
    lv_label_set_text(vol_dn_l, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(vol_dn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_dn_l, i18n_font(), 0);
    lv_obj_center(vol_dn_l);

    ui_Radio_label_vol = lv_label_create(info);
    lv_label_set_text_fmt(ui_Radio_label_vol, tr(I18N_VOL_N), radio_get_volume());
    lv_obj_set_style_text_color(ui_Radio_label_vol, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_Radio_label_vol, i18n_font(), 0);
    lv_obj_align(ui_Radio_label_vol, LV_ALIGN_BOTTOM_LEFT, 36, -10);

    lv_obj_t *vol_up = lv_btn_create(info);
    lv_obj_set_size(vol_up, 32, 32);
    lv_obj_align(vol_up, LV_ALIGN_BOTTOM_LEFT, 80, 0);
    lv_obj_add_style(vol_up, &style_vol_btn, 0);
    lv_obj_add_event_cb(vol_up, ui_event_Radio_btn_vol_up, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_up_l = lv_label_create(vol_up);
    lv_label_set_text(vol_up_l, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(vol_up_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_up_l, i18n_font(), 0);
    lv_obj_center(vol_up_l);

    lv_obj_t *btn = lv_btn_create(info);
    lv_obj_set_size(btn, 50, 38);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_style(btn, &style_play_btn, 0);
    lv_obj_add_event_cb(btn, ui_event_Radio_btn_play, LV_EVENT_CLICKED, NULL);
    ui_Radio_label_play_btn = lv_label_create(btn);
    lv_label_set_text(ui_Radio_label_play_btn, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(ui_Radio_label_play_btn, lv_color_white(), 0);
    lv_obj_set_style_text_font(ui_Radio_label_play_btn, i18n_font(), 0);
    lv_obj_center(ui_Radio_label_play_btn);

    if (!g_poll_timer) {
        g_poll_timer = lv_timer_create(radio_status_poll_cb, 250, NULL);
    }

    /* 订阅事件总线：音量变更时同步 "Vol N" 标签 */
    event_bus_subscribe(EVENT_AUDIO_VOLUME_CHANGED, on_audio_volume_changed_evt, NULL);
}

/* ===== 5. tile 清理函数 ===== */
/* (已在上方定义 ui_Radio_cleanup) */
