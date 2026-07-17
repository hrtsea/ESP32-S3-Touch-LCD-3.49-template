#include "ui_events.h"

#include <time.h>
#include "esp_log.h"
#include "event_bus.h"
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

#define COLOR_PRIMARY   lv_color_hex(0x40E0D0)
#define COLOR_WARNING   lv_color_make(0xff, 0xa0, 0x40)
#define COLOR_CRITICAL  lv_color_make(0xff, 0x40, 0x40)
#define COLOR_LED_GREEN lv_color_make(0x40, 0xff, 0x40)
#define COLOR_LED_GRAY  lv_color_make(0x40, 0x40, 0x40)

static const char *TAG = "ui_events";

static lv_timer_t *s_dim_timer = NULL;
static lv_obj_t *s_ip_label = NULL;

static int s_current_tile_idx = 0;

extern lv_obj_t *s_meter_cpu;
extern lv_obj_t *s_meter_temp;
extern lv_obj_t *s_label_cpu_percent;
extern lv_obj_t *s_label_temp_val;
extern lv_meter_indicator_t *s_cpu_arc_val;
extern lv_meter_indicator_t *s_cpu_needle;
extern lv_meter_indicator_t *s_temp_arc_val;
extern lv_meter_indicator_t *s_temp_needle;
extern lv_obj_t *s_bar_mem;
extern lv_obj_t *s_label_mem_percent;
extern lv_obj_t *s_bar_disk;
extern lv_obj_t *s_label_disk_percent;

extern lv_obj_t *s_storage_label_time;
extern lv_obj_t *s_storage_label_up;
extern lv_obj_t *s_storage_label_down;
extern lv_obj_t *s_storage_label_ip;

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

static void on_tick_1hz_evt(const event_t *evt, void *user_data)
{
    (void)evt;
    (void)user_data;

    if (!lvgl_lock(50)) return;

    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    static char time_str[9];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    if (s_label_time != NULL) lv_label_set_text(s_label_time, time_str);
    if (s_storage_label_time != NULL) lv_label_set_text(s_storage_label_time, time_str);

    float tx_speed = data_source_get_tx_speed_mbps();
    static char tx_str[16];
    if (tx_speed < 0.01f) {
        snprintf(tx_str, sizeof(tx_str), "▲ 0.00KB/s");
    } else if (tx_speed < 1.0f) {
        snprintf(tx_str, sizeof(tx_str), "▲ %.2fKB/s", tx_speed * 1024.0f);
    } else {
        snprintf(tx_str, sizeof(tx_str), "▲ %.2fMB/s", tx_speed);
    }
    if (s_label_up != NULL) lv_label_set_text(s_label_up, tx_str);
    if (s_storage_label_up != NULL) lv_label_set_text(s_storage_label_up, tx_str);

    float rx_speed = data_source_get_rx_speed_mbps();
    static char rx_str[16];
    if (rx_speed < 0.01f) {
        snprintf(rx_str, sizeof(rx_str), "▼ 0.00KB/s");
    } else if (rx_speed < 1.0f) {
        snprintf(rx_str, sizeof(rx_str), "▼ %.2fKB/s", rx_speed * 1024.0f);
    } else {
        snprintf(rx_str, sizeof(rx_str), "▼ %.2fMB/s", rx_speed);
    }
    if (s_label_down != NULL) lv_label_set_text(s_label_down, rx_str);
    if (s_storage_label_down != NULL) lv_label_set_text(s_storage_label_down, rx_str);

    char ip_buf[16];
    wifi_cfg_get_current_ip(ip_buf, sizeof(ip_buf));
    static char ip_str[24];
    if (ip_buf[0]) {
        snprintf(ip_str, sizeof(ip_str), "IP: %s", ip_buf);
    } else {
        snprintf(ip_str, sizeof(ip_str), "IP: --");
    }
    if (s_label_ip != NULL) lv_label_set_text(s_label_ip, ip_str);
    if (s_storage_label_ip != NULL) lv_label_set_text(s_storage_label_ip, ip_str);

    if (s_icon_wifi != NULL) {
        char ssid_buf[33];
        wifi_cfg_get_current_ssid(ssid_buf, sizeof(ssid_buf));
        if (wifi_cfg_is_connected()) {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0x80, 0xff, 0x80), 0);
        } else if (ssid_buf[0]) {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0xff, 0xa0, 0x40), 0);
        } else {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0x40, 0x40, 0x40), 0);
        }
    }

    bool nas_connected = data_source_is_connected();
    if (s_icon_bt != NULL) {
        lv_obj_set_style_text_color(s_icon_bt, nas_connected ?
            lv_color_make(0x80, 0xff, 0x80) : lv_color_make(0xff, 0x40, 0x40), 0);
    }

    lvgl_unlock();
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

static void on_backlight_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(uint8_t)) return;
    uint8_t bri = *(uint8_t *)evt->data;
    if (ui_helpers_get_dim_state() == 0) {
        ui_helpers_backlight_apply(bri);
    }
}

static void on_show_fps_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(uint8_t)) return;
    uint8_t show = *(uint8_t *)evt->data;
    lv_obj_t *fps_lbl = disp_driver_get_fps_label();
    if (fps_lbl) {
        if (show) lv_obj_clear_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(fps_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_tile_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(int)) return;
    int idx = *(int *)evt->data;
    lv_obj_t *tv = ui_helpers_get_tileview();
    if (!tv) return;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    lv_obj_set_tile_id(tv, idx, 0, LV_ANIM_OFF);
}

static void on_cfg_changed_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || !evt->data || evt->data_len < sizeof(cfg_change_info_t)) return;
    const cfg_change_info_t *info = (const cfg_change_info_t *)evt->data;
    switch (info->field) {
        case CFG_FIELD_BG_MODE:
            if (g_cfg.bg_mode == 2) bg_fetcher_ensure();
            break;
        case CFG_FIELD_BG_URL:
            if (g_cfg.bg_mode == 2) bg_fetcher_ensure();
            break;
        default:
            break;
    }
}

static void on_nas_data_update_evt(const event_t *evt, void *user_data)
{
    (void)user_data;
    if (!evt || evt->id != EVENT_NAS_DATA_UPDATE) {
        ESP_LOGW(TAG, "Invalid NAS data event");
        return;
    }

    const NasData *data = (const NasData *)evt->data;
    if (!data || !data->is_online) {
        ESP_LOGW(TAG, "NAS data event with invalid data");
        return;
    }

    ESP_LOGD(TAG, "Received NAS data update (cpu=%.1f%%, temp=%d°C, mem=%.1f%%)",
             data->system.cpu_pct, data->system.temp_cpu, data->system.ram_pct);

    if (!lvgl_lock(50)) {
        ESP_LOGD(TAG, "LVGL lock failed, skipping UI update");
        return;
    }

    int cpu_pct = (int)data->system.cpu_pct;
    int temp = data->system.temp_cpu;
    int mem_pct = (int)data->system.ram_pct;
    int disk_pct = 50;

    if (data->volume_count > 0) {
        disk_pct = (int)data->volumes[0].used_pct;
    } else if (data->disk_count > 0) {
        disk_pct = (int)data->disks[0].used_pct;
    }

    if (s_meter_cpu && s_cpu_arc_val && s_cpu_needle && s_label_cpu_percent) {
        cpu_pct = (cpu_pct < 0) ? 0 : (cpu_pct > 100) ? 100 : cpu_pct;
        lv_meter_set_indicator_end_value(s_meter_cpu, s_cpu_arc_val, cpu_pct);
        lv_meter_set_indicator_value(s_meter_cpu, s_cpu_needle, cpu_pct);
        static char cpu_str[8];
        snprintf(cpu_str, sizeof(cpu_str), "%d%%", cpu_pct);
        lv_label_set_text(s_label_cpu_percent, cpu_str);
    }

    if (s_meter_temp && s_temp_arc_val && s_temp_needle && s_label_temp_val) {
        temp = (temp < 0) ? 0 : (temp > 100) ? 100 : temp;
        lv_meter_set_indicator_end_value(s_meter_temp, s_temp_arc_val, temp);
        lv_meter_set_indicator_value(s_meter_temp, s_temp_needle, temp);
        static char temp_str[10];
        snprintf(temp_str, sizeof(temp_str), "%d°C", temp);
        lv_label_set_text(s_label_temp_val, temp_str);
    }

    if (s_bar_mem && s_label_mem_percent) {
        mem_pct = (mem_pct < 0) ? 0 : (mem_pct > 100) ? 100 : mem_pct;
        lv_bar_set_value(s_bar_mem, mem_pct, LV_ANIM_ON);
        static char mem_str[8];
        snprintf(mem_str, sizeof(mem_str), "%d%%", mem_pct);
        lv_label_set_text(s_label_mem_percent, mem_str);
    }

    if (s_bar_disk && s_label_disk_percent) {
        disk_pct = (disk_pct < 0) ? 0 : (disk_pct > 100) ? 100 : disk_pct;
        lv_bar_set_value(s_bar_disk, disk_pct, LV_ANIM_ON);
        static char disk_str[8];
        snprintf(disk_str, sizeof(disk_str), "%d%%", disk_pct);
        lv_label_set_text(s_label_disk_percent, disk_str);
    }

    for (int i = 0; i < MAX_HDD_INDICATORS; i++) {
        if (s_hdd_leds[i] && i < data->disk_count) {
            const NasDiskInfo *disk = &data->disks[i];
            if (disk->health == HEALTH_OK) {
                lv_obj_set_style_bg_color(s_hdd_leds[i], COLOR_LED_GREEN, 0);
            } else if (disk->health == HEALTH_WARNING) {
                lv_obj_set_style_bg_color(s_hdd_leds[i], COLOR_WARNING, 0);
            } else if (disk->health == HEALTH_CRITICAL) {
                lv_obj_set_style_bg_color(s_hdd_leds[i], COLOR_CRITICAL, 0);
            } else {
                lv_obj_set_style_bg_color(s_hdd_leds[i], COLOR_LED_GRAY, 0);
            }
            if (s_hdd_labels[i]) {
                lv_label_set_text_fmt(s_hdd_labels[i], "%s", disk->name[0] ? disk->name : "HDD");
            }
        } else if (s_hdd_leds[i]) {
            lv_obj_set_style_bg_color(s_hdd_leds[i], COLOR_LED_GRAY, 0);
        }
    }

    for (int i = 0; i < MAX_HDD_BARS; i++) {
        if (i < data->disk_count) {
            const NasDiskInfo *disk = &data->disks[i];

            if (s_hdd_names[i]) {
                const char *name = disk->name[0] ? disk->name : "HDD";
                lv_label_set_text(s_hdd_names[i], name);
            }

            if (s_hdd_bars[i]) {
                int pct = (disk->used_pct > 100) ? 100 : disk->used_pct;
                lv_bar_set_value(s_hdd_bars[i], pct, LV_ANIM_ON);

                lv_color_t color = COLOR_PRIMARY;
                if (disk->health == HEALTH_WARNING) {
                    color = COLOR_WARNING;
                } else if (disk->health == HEALTH_CRITICAL) {
                    color = COLOR_CRITICAL;
                }
                lv_obj_set_style_bg_color(s_hdd_bars[i], color, LV_PART_INDICATOR);
            }

            if (s_hdd_percents[i]) {
                int pct = (disk->used_pct > 100) ? 100 : disk->used_pct;
                static char pct_str[8];
                snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
                lv_label_set_text(s_hdd_percents[i], pct_str);
            }

            if (s_hdd_temps[i]) {
                if (disk->temp > 0) {
                    static char temp_str[16];
                    snprintf(temp_str, sizeof(temp_str), "%d°C", disk->temp);
                    lv_label_set_text(s_hdd_temps[i], temp_str);
                } else {
                    lv_label_set_text(s_hdd_temps[i], "--°C");
                }
            }

            if (s_hdd_names[i]) lv_obj_clear_flag(s_hdd_names[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_bars[i]) lv_obj_clear_flag(s_hdd_bars[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_percents[i]) lv_obj_clear_flag(s_hdd_percents[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_temps[i]) lv_obj_clear_flag(s_hdd_temps[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_hdd_names[i]) lv_obj_add_flag(s_hdd_names[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_bars[i]) lv_obj_add_flag(s_hdd_bars[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_percents[i]) lv_obj_add_flag(s_hdd_percents[i], LV_OBJ_FLAG_HIDDEN);
            if (s_hdd_temps[i]) lv_obj_add_flag(s_hdd_temps[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_unlock();
}

void ui_events_subscribe_events(void)
{
    event_bus_subscribe(EVENT_BACKLIGHT_CHANGED, on_backlight_changed_evt, NULL);
    event_bus_subscribe(EVENT_SHOW_FPS_CHANGED, on_show_fps_changed_evt, NULL);
    event_bus_subscribe(EVENT_TILE_CHANGED, on_tile_changed_evt, NULL);
    event_bus_subscribe(EVENT_NAS_DATA_UPDATE, on_nas_data_update_evt, NULL);
    event_bus_subscribe(EVENT_CFG_CHANGED, on_cfg_changed_evt, NULL);
    event_bus_subscribe(EVENT_TICK_1HZ, on_tick_1hz_evt, NULL);
}

void ui_events_unsubscribe_events(void)
{
    event_bus_unsubscribe(EVENT_BACKLIGHT_CHANGED, on_backlight_changed_evt);
    event_bus_unsubscribe(EVENT_SHOW_FPS_CHANGED, on_show_fps_changed_evt);
    event_bus_unsubscribe(EVENT_TILE_CHANGED, on_tile_changed_evt);
    event_bus_unsubscribe(EVENT_NAS_DATA_UPDATE, on_nas_data_update_evt);
    event_bus_unsubscribe(EVENT_CFG_CHANGED, on_cfg_changed_evt);
    event_bus_unsubscribe(EVENT_TICK_1HZ, on_tick_1hz_evt);
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



static char _ssid_buf[64];

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
    lv_event_code_t event_code = lv_event_get_code(e);
    
    if (event_code == LV_EVENT_CLICKED) {
        if (ui_Screen_Storage == NULL) {
            ui_Screen_Storage_screen_init();
        }
        lv_scr_load_anim(ui_Screen_Storage, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

void ui_event_Screen_Overview_gesture(lv_event_t* e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *current_scr = lv_scr_act();
    
    ESP_LOGI("Events", "Overview gesture event: code=%d, target=%p, current_scr=%p, Screen_Overview=%p", 
             (int)event_code, (void*)target, (void*)current_scr, (void*)ui_Screen_Overview);
    
    if (event_code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) {
            ESP_LOGW("Events", "No active input device!");
            return;
        }
        
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        
        ESP_LOGI("Events", "Overview gesture detected: dir=%d (1=LEFT, 2=RIGHT, 3=TOP, 4=BOTTOM), point=(%d,%d)", 
                 (int)dir, point.x, point.y);
        
        if (dir == LV_DIR_RIGHT) {
            ESP_LOGI("Events", "Swiping RIGHT -> Loading Settings screen");
            if (ui_Screen_Settings == NULL) {
                ESP_LOGI("Events", "Settings screen not created, initializing...");
                ui_Screen_Settings_screen_init();
            }
            ESP_LOGI("Events", "Settings screen pointer: %p", (void*)ui_Screen_Settings);
            lv_scr_load_anim(ui_Screen_Settings, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            ESP_LOGI("Events", "Screen load animation initiated");
        } else if (dir == LV_DIR_LEFT) {
            ESP_LOGI("Events", "Swiping LEFT -> Loading Storage screen");
            if (ui_Screen_Storage == NULL) {
                ESP_LOGI("Events", "Storage screen not created, initializing...");
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
        
        ESP_LOGI("Events", "Settings gesture detected: dir=%d", (int)dir);
        
        if (dir == LV_DIR_LEFT) {
            ESP_LOGI("Events", "Swiping LEFT -> Loading Overview screen");
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
        
        ESP_LOGI("Events", "Storage gesture detected: dir=%d", (int)dir);
        
        if (dir == LV_DIR_RIGHT) {
            ESP_LOGI("Events", "Swiping RIGHT -> Loading Overview screen");
            if (ui_Screen_Overview == NULL) {
                ui_Screen_Overview_screen_init();
            }
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        }
    }
}

void resetScreenOffTimer(lv_event_t * e)
{
    ESP_LOGD("Events", "resetScreenOffTimer called");
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

    ESP_LOGI("Events", "WiFi connect: ssid=%s pass_len=%u",
             ssid, (unsigned)strlen(password));

    app_cfg_wifi_connect_save(ssid, password);
}

void scanNetwork(lv_event_t * e)
{
    ESP_LOGI("Events", "scanNetwork called");
    wifi_start_scan();
}

void toggleWiFi(lv_event_t * e)
{
    bool enabled = lv_obj_has_state(ui_Settings_Switch_Wifi, LV_STATE_CHECKED);
    ESP_LOGI("Events", "toggleWiFi called: %s", enabled ? "ON" : "OFF");

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
    uint16_t selected = lv_dropdown_get_selected(ui_Settings_Dropdown_Brightness);
    ESP_LOGI("Events", "setBrightness called: %d", selected);
}

void setTimer(lv_event_t * e)
{
    uint16_t selected = lv_dropdown_get_selected(ui_Settings_Dropdown_SleepTimer);
    const char *options[] = {"0", "15", "30", "60"};
    int seconds = atoi(options[selected]);
    ESP_LOGI("Events", "setTimer called: %d seconds", seconds);
}

void setWallpaper(lv_event_t * e)
{
    ESP_LOGD("Events", "setWallpaper called");
}

void loadStationFromSDCARD(lv_event_t * e)
{
    ESP_LOGD("Events", "loadStationFromSDCARD called");
}

void loadMusicFromSDCARD(lv_event_t * e)
{
    ESP_LOGD("Events", "loadMusicFromSDCARD called");
}

void set_query_para_autoip(lv_event_t * e)
{
    ESP_LOGD("Events", "set_query_para_autoip called");
}

void setOffsetHour(lv_event_t * e)
{
    ESP_LOGD("Events", "setOffsetHour called");
}

void setOffsetMinute(lv_event_t * e)
{
    ESP_LOGD("Events", "setOffsetMinute called");
}

void setTempUnit(lv_event_t * e)
{
    ESP_LOGD("Events", "setTempUnit called");
}

void saveConfig(lv_event_t * e)
{
    app_cfg_save();
    ESP_LOGI("Events", "saveConfig called");
}

void turnonScreen(lv_event_t * e)
{
    ESP_LOGD("Events", "turnonScreen called");
}

