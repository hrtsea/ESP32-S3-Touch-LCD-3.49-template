#include "ui_events.h"

#include <time.h>
#include <math.h>
#include "esp_log.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "disp_driver.h"
#include "esp_wifi_config.h"
#include "wifi_adapter.h"
#include "app_cfg.h"
#include "ui_helpers.h"
#include "../data/nas_data.h"
#include "../data/data_source.h"
#include "utils/bg_fetcher.h"
#include "screens/ui_Screen_Overview.h"
#include "screens/ui_Screen_Settings.h"
#include "screens/ui_Screen_Storage.h"
#include "screens/ui_Screen_Boot.h"
#include "screens/ui_Screen_DiskDetail.h"
#include "screens/ui_Screen_SystemDetail.h"
#include "../data/fan_control.h"

#define UI_UPDATE(code) do { \
    if (lvgl_lock(50)) { \
        code; \
        lvgl_unlock(); \
    } \
} while(0)

static const char *TAG = "ui_events";

static lv_timer_t *s_dim_timer = NULL;
static lv_obj_t *s_ip_label = NULL;

static int s_current_tile_idx = 0;

static TaskHandle_t s_ui_task_hdl = NULL;
static bool s_running = false;

static void ip_label_ensure(void)
{
    if (s_ip_label) return;
    s_ip_label = lv_label_create(lv_layer_top());
    lv_label_set_text(s_ip_label, "");
    lv_obj_set_style_text_color(s_ip_label, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(s_ip_label, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_ip_label, LV_OPA_50, 0);
    lv_obj_set_style_pad_left(s_ip_label, 4, 0);
    lv_obj_set_style_pad_right(s_ip_label, 4, 0);
    lv_obj_set_style_pad_top(s_ip_label, 1, 0);
    lv_obj_set_style_pad_bottom(s_ip_label, 1, 0);
    lv_obj_set_style_radius(s_ip_label, 3, 0);
    lv_obj_align(s_ip_label, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_add_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_events_wifi_status_cb(bool connected, const char *ip_addr)
{
    if (!lvgl_lock(50)) return;
    ip_label_ensure();
    if (connected && ip_addr && *ip_addr) {
        lv_label_set_text(s_ip_label, ip_addr);
        lv_obj_clear_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ip_label, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

static void dim_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t idle_ms = lv_tick_elaps(ui_helpers_get_last_activity_ms());
    int want = 0;

    if (g_cfg.off_s > 0 && idle_ms >= (uint32_t)g_cfg.off_s * 1000) {
        want = 2;
    } else if (g_cfg.dim_s > 0 && idle_ms >= (uint32_t)g_cfg.dim_s * 1000) {
        want = 1;
    }

    if (want != ui_helpers_get_dim_state()) {
        ui_helpers_set_dim_state(want);
        if (want == 0) {
            ui_helpers_backlight_apply(g_cfg.brightness);
        } else if (want == 1) {
            ui_helpers_backlight_apply(g_cfg.brightness / 8 + 4);
        } else {
            ui_helpers_backlight_apply(0);
        }
    }
}

static void time_update_timer_cb(lv_timer_t *t)
{
    (void)t;
    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    static char time_str[9];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    overview_screen_update_time(time_str);
    storage_screen_update_time(time_str);
}

void ui_events_start_time_timer(void)
{
    static bool created = false;
    if (!created) {
        lv_timer_create(time_update_timer_cb, 1000, NULL);
        created = true;
    }
}

void ui_events_start_dim_timer(void)
{
    if (s_dim_timer) return;
    s_dim_timer = lv_timer_create(dim_timer_cb, 1000, NULL);
}

void ui_events_stop_dim_timer(void)
{
    if (s_dim_timer) {
        lv_timer_del(s_dim_timer);
        s_dim_timer = NULL;
    }
}

static float s_last_tx_speed = 0.0f;
static float s_last_rx_speed = 0.0f;

static void on_nas_data_update_evt(const NasData *data)
{
    if (!data) {
        return;
    }

    if (data->is_online) {
        overview_screen_update_cpu((int)data->system.cpu_pct);
        overview_screen_update_temp(data->system.temp_cpu);
        overview_screen_update_mem((int)data->system.ram_pct);
        overview_screen_update_disk((int)data->system.disk_pct);

        uint8_t total_slots = config_get_total_disk_slots();
        if (total_slots == 0) total_slots = 1;

        ESP_LOGI(TAG, "[ui disk] disk_slot_count=%u total_slots=%u sata=%u m2=%u",
                 data->disk_slot_count, total_slots,
                 g_config.sata_disk_count, g_config.m2_disk_count);

        for (int i = 0; i < total_slots; i++) {
            if (i < data->disk_slot_count) {
                const NasDiskInfo *disk = &data->disks[i];
                overview_screen_update_hdd_led(i, disk->online, disk->health);
                overview_screen_update_hdd_name(i, disk->name);
            }
        }

        for (int i = 0; i < data->disk_slot_count; i++) {
            const NasDiskInfo *disk = &data->disks[i];
            storage_screen_update_hdd_name(i, disk->name);
            storage_screen_update_hdd_bar(i, (int)disk->used_pct, disk->health);
            storage_screen_update_hdd_temp(i, disk->temp);
            storage_screen_update_hdd_online(i, disk->online);
        }

        float tx_speed = (float)data->network.tx_bps / 8000000.0f;
        float rx_speed = (float)data->network.rx_bps / 8000000.0f;
        if (fabs(tx_speed - s_last_tx_speed) >= 0.05f ||
            fabs(rx_speed - s_last_rx_speed) >= 0.05f) {
            overview_screen_update_network((int)(data->network.tx_bps / 1000), (int)(data->network.rx_bps / 1000));
            storage_screen_update_network((int)(data->network.tx_bps / 1000), (int)(data->network.rx_bps / 1000));
            s_last_tx_speed = tx_speed;
            s_last_rx_speed = rx_speed;
        }
    }

    /* 推送温度到风扇控制任务（独立于 UI 更新） */
    fan_control_on_nas_data(data);

    /* 详情屏数据更新（仅当屏存在时） */
    if (ui_Screen_DiskDetail != NULL) {
        ui_Screen_DiskDetail_update_data(data);
    }
    if (ui_Screen_SystemDetail != NULL) {
        ui_Screen_SystemDetail_update_data(data);
    }
}

static void on_wifi_state_changed(bool connected)
{
    char ip_buf[16];
    wifi_cfg_get_current_ip(ip_buf, sizeof(ip_buf));
    overview_screen_update_ip(ip_buf);
    storage_screen_update_ip(ip_buf);
    overview_screen_update_wifi(connected);
}

static void task_ui_event_loop(void *arg)
{
    (void)arg;

    while (s_running) {
        event_t evt;
        if (!event_bus_receive(&evt, portMAX_DELAY)) {
            continue;
        }

        switch (evt.id) {
            case EVENT_NAS_DATA_UPDATE:
                if (evt.data && evt.data_len >= sizeof(NasData)) {
                    UI_UPDATE(on_nas_data_update_evt((const NasData *)evt.data));
                }
                break;

            case EVENT_WIFI_CONNECTED:
                UI_UPDATE(on_wifi_state_changed(true));
                break;

            case EVENT_WIFI_DISCONNECTED:
                UI_UPDATE(on_wifi_state_changed(false));
                break;

            case EVENT_BACKLIGHT_CHANGED:
                if (evt.data && evt.data_len >= sizeof(uint8_t)) {
                    uint8_t bri = *(uint8_t *)evt.data;
                    UI_UPDATE(
                        if (ui_helpers_get_dim_state() == 0) {
                            ui_helpers_backlight_apply(bri);
                        }
                    );
                }
                break;

            case EVENT_SHOW_FPS_CHANGED:
                if (evt.data && evt.data_len >= sizeof(uint8_t)) {
                    uint8_t show = *(uint8_t *)evt.data;
                    UI_UPDATE(
                        lv_obj_t *fps_lbl = disp_driver_get_fps_label();
                        if (fps_lbl) {
                            if (show) lv_obj_clear_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
                            else lv_obj_add_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
                        }
                    );
                }
                break;

            case EVENT_TILE_CHANGED:
                if (evt.data && evt.data_len >= sizeof(int)) {
                    int idx = *(int *)evt.data;
                    UI_UPDATE(
                        lv_obj_t *tv = ui_helpers_get_tileview();
                        if (tv) {
                            if (idx < 0) idx = 0;
                            if (idx > 5) idx = 5;
                            lv_obj_set_tile_id(tv, idx, 0, LV_ANIM_OFF);
                        }
                    );
                }
                break;

            case EVENT_DISK_CONFIG_CHANGED:
                UI_UPDATE(
                    if (ui_Screen_Overview) {
                        ui_Screen_Overview_screen_destroy();
                        ui_Screen_Overview_screen_init();
                    }
                    if (ui_Screen_Storage) {
                        ui_Screen_Storage_screen_destroy();
                        ui_Screen_Storage_screen_init();
                    }
                );
                break;

            case EVENT_FAN_CONFIG_CHANGED:
                fan_control_apply_config(&g_config.fan);
                break;

            case EVENT_CFG_CHANGED:
                if (evt.data && evt.data_len >= sizeof(cfg_change_info_t)) {
                    const cfg_change_info_t *info = (const cfg_change_info_t *)evt.data;
                    if (info->field == CFG_FIELD_BG_MODE || info->field == CFG_FIELD_BG_URL) {
                        if (g_cfg.bg_mode == 2) bg_fetcher_ensure();
                    }
                }
                break;

            default:
                break;
        }
    }

    vTaskDelete(NULL);
}

void ui_events_start(void)
{
    if (s_running) {
        return;
    }

    s_running = true;
    xTaskCreate(task_ui_event_loop, "ui_event_loop", 8192, NULL, 1, &s_ui_task_hdl);
    ui_events_start_time_timer();
}

void ui_events_stop(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;

    if (s_ui_task_hdl != NULL) {
        vTaskDelete(s_ui_task_hdl);
        s_ui_task_hdl = NULL;
    }
}

static void tileview_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    lv_obj_t *tv = lv_event_get_target(e);
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    const int last = 3;

    if (dir == LV_DIR_LEFT && idx == last) {
        lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    } else if (dir == LV_DIR_RIGHT && idx == 0) {
        lv_obj_set_tile_id(tv, last, 0, LV_ANIM_OFF);
    }
}

static void tileview_commit_cb(lv_event_t *e)
{
    static lv_coord_t s_press_x = 0;
    static lv_coord_t s_locked_x = 0;
    static bool s_committed = false;

    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *tv = lv_event_get_target(e);

    if (c == LV_EVENT_PRESSED) {
        lv_indev_t *id = lv_indev_get_act();
        lv_point_t p; lv_indev_get_point(id, &p);
        s_press_x = p.x;
        s_locked_x = lv_obj_get_scroll_x(tv);
        s_committed = false;
    } else if (c == LV_EVENT_SCROLL && !s_committed) {
        lv_indev_t *id = lv_indev_get_act();
        if (!id) return;
        lv_point_t p; lv_indev_get_point(id, &p);
        int dx = (int)p.x - (int)s_press_x;
        if (dx > 20 || dx < -20) {
            s_committed = true;
        } else {
            lv_obj_scroll_to_x(tv, s_locked_x, LV_ANIM_OFF);
        }
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        s_committed = false;
    }
}

static void screen_scroll_stamp_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_SCROLL) {
        ui_helpers_set_last_scroll_ms(lv_tick_get());
    }
}

static void tile_monitor_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *tv = ui_helpers_get_tileview();
    if (!tv) return;
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t w = lv_obj_get_width(tv);
    int idx = (w > 0) ? (x + w / 2) / w : 0;
    if (idx < 0) idx = 0;
    if (idx >= 4) idx = 3;
    if (idx != s_current_tile_idx) {
        s_current_tile_idx = idx;
    }
}

void ui_events_activity_kick(lv_event_t *e)
{
    (void)e;
    ui_helpers_notify_activity();
}

void ui_events_register_tileview_events(lv_obj_t *tv)
{
    lv_obj_add_event_cb(tv, tileview_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(tv, tileview_commit_cb, LV_EVENT_ALL, NULL);
}

void ui_events_register_screen_events(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, ui_events_activity_kick, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, ui_events_activity_kick, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, screen_scroll_stamp_cb, LV_EVENT_ALL, NULL);
}

void ui_events_start_tile_monitor(void)
{
    static bool created = false;
    if (!created) {
        lv_timer_create(tile_monitor_cb, 100, NULL);
        created = true;
    }
}

void ui_Screen_Boot_event_handler(lv_event_t* e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_CLICKED) {
        ui_Screen_Boot_stop_timeout();
        if (ui_Screen_Overview == NULL) {
            ui_Screen_Overview_screen_init();
        }
        lv_scr_load(ui_Screen_Overview);
    }
}

void ui_event_Screen_Overview_hdd_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    uint8_t *idx_ptr = (uint8_t *)lv_event_get_user_data(e);
    uint8_t disk_idx = idx_ptr ? *idx_ptr : 0;

    if (ui_Screen_DiskDetail == NULL) {
        ui_Screen_DiskDetail_screen_init(disk_idx);
    } else {
        ui_Screen_DiskDetail_screen_destroy();
        ui_Screen_DiskDetail_screen_init(disk_idx);
    }
    lv_scr_load_anim(ui_Screen_DiskDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_event_Screen_Overview_cpu_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_SystemDetail == NULL) {
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_CPU);
    } else {
        ui_Screen_SystemDetail_screen_destroy();
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_CPU);
    }
    lv_scr_load_anim(ui_Screen_SystemDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_event_Screen_Overview_mem_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_SystemDetail == NULL) {
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_MEM);
    } else {
        ui_Screen_SystemDetail_screen_destroy();
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_MEM);
    }
    lv_scr_load_anim(ui_Screen_SystemDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_event_Screen_Overview_gesture(lv_event_t* e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;

        lv_dir_t dir = lv_indev_get_gesture_dir(indev);

        if (dir == LV_DIR_RIGHT) {
            if (ui_Screen_Settings == NULL) {
                ui_Screen_Settings_screen_init();
            }
            lv_scr_load_anim(ui_Screen_Settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        } else if (dir == LV_DIR_LEFT) {
            if (ui_Screen_Storage == NULL) {
                ui_Screen_Storage_screen_init();
            }
            lv_scr_load_anim(ui_Screen_Storage, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        }
    }
}

void ui_event_Screen_Settings_gesture(lv_event_t* e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if (dir == LV_DIR_LEFT) {
            if (ui_Screen_Overview == NULL) {
                ui_Screen_Overview_screen_init();
            }
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        }
    }
}

void ui_event_Screen_Storage_gesture(lv_event_t* e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

        if (dir == LV_DIR_RIGHT) {
            if (ui_Screen_Overview == NULL) {
                ui_Screen_Overview_screen_init();
            }
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        }
    }
}

void resetScreenOffTimer(lv_event_t * e)
{
    (void)e;
}

void saveWiFiCredential(lv_event_t * e)
{
    char sel_buf[128];
    lv_dropdown_get_selected_str(ui_Settings_Dropdown_NetworkList, sel_buf, sizeof(sel_buf));
    const char *password = lv_textarea_get_text(ui_Settings_Textarea_Password);

    if (sel_buf[0] == '\0' || !password) return;

    char ssid[33] = {0};
    const char *p = sel_buf;
    if (strncmp(p, LV_SYMBOL_OK " ", strlen(LV_SYMBOL_OK " ")) == 0) {
        p += strlen(LV_SYMBOL_OK " ");
    }
    const char *paren = strrchr(p, '(');
    size_t ssid_len = paren ? (size_t)(paren - p) : strlen(p);
    if (ssid_len > 0 && p[ssid_len - 1] == ' ') ssid_len--;
    if (ssid_len > sizeof(ssid) - 1) ssid_len = sizeof(ssid) - 1;
    memcpy(ssid, p, ssid_len);
    ssid[ssid_len] = '\0';

    if (ssid[0] == '\0') return;

    app_cfg_wifi_connect_save(ssid, password);
}

void scanNetwork(lv_event_t * e)
{
    (void)e;
    wifi_start_scan();
}

void toggleWiFi(lv_event_t * e)
{
    bool enabled = lv_obj_has_state(ui_Settings_Switch_Wifi, LV_STATE_CHECKED);

    if (enabled) {
        if (!wifi_is_connected() && wifi_has_credentials()) {
            char ssid[33] = {0};
            char pass[65] = {0};
            wifi_cfg_get_current_ssid(ssid, sizeof(ssid));
            if (ssid[0] == '\0') {
                app_cfg_get_last_ssid(ssid, sizeof(ssid));
            }
            wifi_network_t net;
            if (ssid[0] && wifi_cfg_get_network(ssid, &net) == ESP_OK) {
                strncpy(pass, net.password, sizeof(pass) - 1);
                wifi_connect(ssid, pass);
            }
        }
    } else {
        wifi_disconnect();
    }
}

void setBrightness(lv_event_t * e)
{
    (void)e;
}

void setTimer(lv_event_t * e)
{
    (void)e;
}

void setWallpaper(lv_event_t * e)
{
    (void)e;
}

void loadStationFromSDCARD(lv_event_t * e)
{
    (void)e;
}

void loadMusicFromSDCARD(lv_event_t * e)
{
    (void)e;
}

void set_query_para_autoip(lv_event_t * e)
{
    (void)e;
}

void setOffsetHour(lv_event_t * e)
{
    (void)e;
}

void setOffsetMinute(lv_event_t * e)
{
    (void)e;
}

void setTempUnit(lv_event_t * e)
{
    (void)e;
}

void saveConfig(lv_event_t * e)
{
    (void)e;
    app_cfg_save();
}

void turnonScreen(lv_event_t * e)
{
    (void)e;
}