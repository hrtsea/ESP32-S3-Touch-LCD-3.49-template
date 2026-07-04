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

static const char *TAG = "ui_radio";

/* ---------------------- Radio tile ---------------------- */

lv_obj_t  *g_radio_status_lbl = NULL;   /* "Connecting...", "Playing", etc. */
lv_obj_t  *g_radio_now_lbl    = NULL;   /* current station name + genre */
lv_obj_t  *g_radio_btn_lbl    = NULL;   /* play / stop glyph */
lv_obj_t  *g_radio_list       = NULL;   /* scrollable station list */
lv_obj_t  *g_radio_vol_lbl    = NULL;   /* "Vol N" indicator */
static bool       g_radio_engine_up  = false;
lv_timer_t *g_radio_poll_timer = NULL;
static int        g_radio_pending_idx = -1;    /* set by the LVGL click cb */

typedef enum {
    RADIO_CMD_NONE = 0,
    RADIO_CMD_PLAY_INDEX,   /* g_radio_pending_idx tells which */
    RADIO_CMD_STOP,
    RADIO_CMD_INIT_ENGINE,  /* boot path: init engine without playing yet */
} radio_cmd_t;

static volatile radio_cmd_t  g_radio_cmd        = RADIO_CMD_NONE;
static char                  g_radio_status[80] = "Idle";
static SemaphoreHandle_t     g_radio_status_mtx = NULL;
static TaskHandle_t          g_radio_worker     = NULL;

static void radio_set_status(const char *s)
{
    if (!g_radio_status_mtx) return;
    xSemaphoreTake(g_radio_status_mtx, portMAX_DELAY);
    strncpy(g_radio_status, s, sizeof(g_radio_status) - 1);
    g_radio_status[sizeof(g_radio_status) - 1] = 0;
    xSemaphoreGive(g_radio_status_mtx);
}

static bool radio_engine_ensure_up(void)
{
    if (g_radio_engine_up) return true;
    /* The radio engine may have been brought up during hw_init (before
       this UI module runs).  Detect that so we don't re-init and don't
       wrongly leave g_radio_engine_up = false. */
    if (radio_get_play_dev() != NULL) {
        g_radio_engine_up = true;
        return true;
    }
    radio_set_status(tr(I18N_RADIO_ENGINE_INIT));
    if (radio_init() != ESP_OK) {
        radio_set_status(tr(I18N_RADIO_ENGINE_FAIL));
        return false;
    }
    g_radio_engine_up = true;
    return true;
}

static void radio_worker_task(void *arg)
{
    (void)arg;
    while (1) {
        radio_cmd_t cmd = g_radio_cmd;
        if (cmd == RADIO_CMD_INIT_ENGINE) {
            g_radio_cmd = RADIO_CMD_NONE;
            radio_engine_ensure_up();
        } else if (cmd == RADIO_CMD_PLAY_INDEX) {
            g_radio_cmd = RADIO_CMD_NONE;
            int idx = g_radio_pending_idx;
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
            g_radio_cmd = RADIO_CMD_NONE;
            if (g_radio_engine_up) radio_stop();
            radio_set_status(tr(I18N_RADIO_STOPPED));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void radio_worker_ensure(void)
{
    if (!g_radio_status_mtx) g_radio_status_mtx = xSemaphoreCreateMutex();
    if (!g_radio_worker) {
        xTaskCreatePinnedToCore(radio_worker_task, "radio_wrk", 6 * 1024,
                                NULL, 4, &g_radio_worker, 1);
    }
}

/* Called from app_main once Wi-Fi is up so the engine is warm by the time
   the user picks a station -- removes the ~200 ms codec/I2S setup from the
   first-play latency. */
void radio_engine_warm_at_boot(void)
{
    radio_worker_ensure();
    g_radio_cmd = RADIO_CMD_INIT_ENGINE;
}

static void radio_status_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_radio_status_lbl || !g_radio_status_mtx) return;
    char snap[80];
    xSemaphoreTake(g_radio_status_mtx, portMAX_DELAY);
    strncpy(snap, g_radio_status, sizeof(snap));
    xSemaphoreGive(g_radio_status_mtx);
    lv_label_set_text(g_radio_status_lbl, snap);
    if (g_radio_btn_lbl) {
        bool playing = g_radio_engine_up && radio_is_playing();
        lv_label_set_text(g_radio_btn_lbl, playing ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    }
    if (g_radio_now_lbl) {
        int idx = radio_current_index();
        if (idx >= 0) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%s  -  %s",
                     radio_station_name(idx), radio_station_genre(idx));
            lv_label_set_text(g_radio_now_lbl, buf);
        }
    }
}

static void radio_vol_step(int delta)
{
    int v = radio_get_volume() + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    radio_set_volume(v);
    if (g_radio_vol_lbl) lv_label_set_text_fmt(g_radio_vol_lbl, tr(I18N_VOL_N), v);
}

static void radio_vol_dn_cb(lv_event_t *e) { (void)e; radio_vol_step(-5); }
static void radio_vol_up_cb(lv_event_t *e) { (void)e; radio_vol_step(+5); }

static void radio_play_btn_cb(lv_event_t *e)
{
    (void)e;
    radio_worker_ensure();
    if (g_radio_engine_up && radio_is_playing()) {
        g_radio_cmd = RADIO_CMD_STOP;
    } else {
        /* Resume the most recent station, or fall back to station 0. */
        int idx = radio_current_index();
        g_radio_pending_idx = idx >= 0 ? idx : 0;
        g_radio_cmd = RADIO_CMD_PLAY_INDEX;
    }
}

static void radio_station_pick_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= radio_station_count()) return;
    radio_worker_ensure();
    g_radio_pending_idx = idx;
    g_radio_cmd = RADIO_CMD_PLAY_INDEX;
}

void build_radio_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* Split horizontally: scrollable station list on the left, now-playing
       info + transport on the right. The 640 px canvas gives ~62/38 split. */
    const int LIST_W = 400;
    const int INFO_W = canvas_w - LIST_W;

    /* ---------- Left: station list ---------- */
    g_radio_list = lv_obj_create(parent);
    lv_obj_remove_style_all(g_radio_list);
    lv_obj_set_size(g_radio_list, LIST_W, canvas_h);
    lv_obj_align(g_radio_list, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_layout(g_radio_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_radio_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_radio_list, 2, 0);
    lv_obj_set_style_pad_all(g_radio_list, 4, 0);
    lv_obj_set_style_bg_color(g_radio_list, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(g_radio_list, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(g_radio_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_radio_list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < radio_station_count(); i++) {
        lv_obj_t *row = lv_btn_create(g_radio_list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 26);
        lv_obj_set_style_bg_color(row, lv_color_make(0x20, 0x20, 0x30), 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_add_event_cb(row, radio_station_pick_cb, LV_EVENT_CLICKED,
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
    lv_obj_t *info = lv_obj_create(parent);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, INFO_W, canvas_h);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(info, lv_color_make(0x18, 0x18, 0x24), 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, 0);
    lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(info, 6, 0);

    lv_obj_t *header = lv_label_create(info);
    lv_label_set_text_fmt(header, LV_SYMBOL_AUDIO "  %s", tr(I18N_RADIO_NOW_PLAYING));
    lv_obj_set_style_text_color(header, lv_color_make(0xa0, 0xa0, 0xc0), 0);
    lv_obj_set_style_text_font(header, i18n_font(), 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    g_radio_now_lbl = lv_label_create(info);
    lv_label_set_long_mode(g_radio_now_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_radio_now_lbl, INFO_W - 12);
    lv_label_set_text(g_radio_now_lbl, tr(I18N_RADIO_NO_STATION));
    lv_obj_set_style_text_color(g_radio_now_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_now_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_now_lbl, LV_ALIGN_TOP_LEFT, 0, 18);

    g_radio_status_lbl = lv_label_create(info);
    lv_label_set_long_mode(g_radio_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_radio_status_lbl, INFO_W - 12);
    lv_label_set_text(g_radio_status_lbl, tr(I18N_IDLE));
    lv_obj_set_style_text_color(g_radio_status_lbl, lv_color_make(0xc0, 0xc0, 0xc0), 0);
    lv_obj_set_style_text_font(g_radio_status_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_status_lbl, LV_ALIGN_TOP_LEFT, 0, 70);

    /* Bottom row: [-]  Vol N  [+]                             [play] */
    lv_obj_t *vol_dn = lv_btn_create(info);
    lv_obj_set_size(vol_dn, 32, 32);
    lv_obj_align(vol_dn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(vol_dn, 4, 0);
    lv_obj_set_style_bg_color(vol_dn, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_add_event_cb(vol_dn, radio_vol_dn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_dn_l = lv_label_create(vol_dn);
    lv_label_set_text(vol_dn_l, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(vol_dn_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_dn_l, i18n_font(), 0);
    lv_obj_center(vol_dn_l);

    g_radio_vol_lbl = lv_label_create(info);
    lv_label_set_text_fmt(g_radio_vol_lbl, tr(I18N_VOL_N), radio_get_volume());
    lv_obj_set_style_text_color(g_radio_vol_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_vol_lbl, i18n_font(), 0);
    lv_obj_align(g_radio_vol_lbl, LV_ALIGN_BOTTOM_LEFT, 36, -10);

    lv_obj_t *vol_up = lv_btn_create(info);
    lv_obj_set_size(vol_up, 32, 32);
    lv_obj_align(vol_up, LV_ALIGN_BOTTOM_LEFT, 80, 0);
    lv_obj_set_style_radius(vol_up, 4, 0);
    lv_obj_set_style_bg_color(vol_up, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_add_event_cb(vol_up, radio_vol_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *vol_up_l = lv_label_create(vol_up);
    lv_label_set_text(vol_up_l, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(vol_up_l, lv_color_white(), 0);
    lv_obj_set_style_text_font(vol_up_l, i18n_font(), 0);
    lv_obj_center(vol_up_l);

    lv_obj_t *btn = lv_btn_create(info);
    lv_obj_set_size(btn, 50, 38);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x20, 0x80, 0x40), 0);
    lv_obj_add_event_cb(btn, radio_play_btn_cb, LV_EVENT_CLICKED, NULL);
    g_radio_btn_lbl = lv_label_create(btn);
    lv_label_set_text(g_radio_btn_lbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(g_radio_btn_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_radio_btn_lbl, i18n_font(), 0);
    lv_obj_center(g_radio_btn_lbl);

    if (!g_radio_poll_timer) {
        g_radio_poll_timer = lv_timer_create(radio_status_poll_cb, 250, NULL);
    }
}
