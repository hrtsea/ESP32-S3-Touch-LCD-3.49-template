#include "ui_Screen_Overview.h"
#include "../ui_events.h"
#include "../../config/config.h"
#include "../../data/nas_data.h"
#include "../../data/data_source.h"
#include "../../network/wifi_manager.h"
#include "../../utils/event_bus.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_14);

lv_obj_t *ui_Screen_Overview = NULL;

#undef COLOR_BG
#undef COLOR_TEXT
#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_PRIMARY   lv_color_hex(0x40E0D0)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_INACTIVE  lv_color_hex(0x333333)
#define COLOR_LED_GREEN lv_color_hex(0x00FF00)
#define COLOR_LED_GRAY  lv_color_hex(0x666666)
#define COLOR_TEMP      lv_color_hex(0xFF8C00)

static lv_obj_t *s_label_time = NULL;
static lv_obj_t *s_label_up = NULL;
static lv_obj_t *s_label_down = NULL;
static lv_obj_t *s_label_ip = NULL;
static lv_obj_t *s_btn_refresh = NULL;
static lv_obj_t *s_icon_wifi = NULL;
static lv_obj_t *s_icon_bt = NULL;

static lv_obj_t *s_meter_cpu = NULL;
static lv_obj_t *s_meter_temp = NULL;
static lv_obj_t *s_label_cpu_percent = NULL;
static lv_obj_t *s_label_temp_val = NULL;
static lv_meter_indicator_t *s_cpu_arc_val = NULL;
static lv_meter_indicator_t *s_cpu_needle = NULL;
static lv_meter_indicator_t *s_temp_arc_val = NULL;
static lv_meter_indicator_t *s_temp_needle = NULL;

static lv_obj_t *s_bar_mem = NULL;
static lv_obj_t *s_label_mem_percent = NULL;
static lv_obj_t *s_bar_disk = NULL;
static lv_obj_t *s_label_disk_percent = NULL;

static lv_timer_t *s_update_timer = NULL;

static void update_timer_cb(lv_timer_t *timer);
static void refresh_btn_cb(lv_event_t *e);

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 640, 35);
    lv_obj_set_style_bg_color(status_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(status_bar);
    static char title_str[32];
    if (strlen(g_config.nas_user) > 0) {
        snprintf(title_str, sizeof(title_str), "%s", g_config.nas_user);
    } else if (strlen(g_config.nas_type) > 0 && strcmp(g_config.nas_type, "mock") != 0) {
        snprintf(title_str, sizeof(title_str), "%s", g_config.nas_type);
    } else {
        snprintf(title_str, sizeof(title_str), "NAS Monitor");
    }
    lv_label_set_text(label_title, title_str);
    lv_obj_set_style_text_color(label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *label_time = lv_label_create(status_bar);
    s_label_time = label_time;
    static char time_str[9];
    time_t now_t = time(NULL);
    struct tm *tm_now = localtime(&now_t);
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    lv_label_set_text(label_time, time_str);
    lv_obj_set_style_text_color(label_time, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
    lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 110, 0);

    lv_obj_t *label_up = lv_label_create(status_bar);
    static char up_str[16];
    snprintf(up_str, sizeof(up_str), "▲ 0.00KB/s");
    lv_label_set_text(label_up, up_str);
    lv_obj_set_style_text_color(label_up, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_up, &lv_font_montserrat_14, 0);
    lv_obj_align(label_up, LV_ALIGN_CENTER, -70, 0);
    s_label_up = label_up;

    lv_obj_t *label_down = lv_label_create(status_bar);
    static char down_str[16];
    snprintf(down_str, sizeof(down_str), "▼ 0.00KB/s");
    lv_label_set_text(label_down, down_str);
    lv_obj_set_style_text_color(label_down, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_down, &lv_font_montserrat_14, 0);
    lv_obj_align(label_down, LV_ALIGN_CENTER, 70, 0);
    s_label_down = label_down;

    s_icon_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_icon_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(s_icon_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(s_icon_wifi, LV_ALIGN_RIGHT_MID, -85, 0);

    s_icon_bt = lv_label_create(status_bar);
    lv_label_set_text(s_icon_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_icon_bt, lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_text_font(s_icon_bt, &lv_font_montserrat_14, 0);
    lv_obj_align(s_icon_bt, LV_ALIGN_RIGHT_MID, -115, 0);

    s_label_ip = lv_label_create(status_bar);
    lv_label_set_text(s_label_ip, "IP: --");
    lv_obj_set_style_text_color(s_label_ip, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_label_ip, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label_ip, LV_ALIGN_RIGHT_MID, -45, 0);

    s_btn_refresh = lv_btn_create(status_bar);
    lv_obj_set_size(s_btn_refresh, 35, 28);
    lv_obj_set_style_bg_color(s_btn_refresh, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(s_btn_refresh, 0, 0);
    lv_obj_set_style_radius(s_btn_refresh, 3, 0);
    lv_obj_align(s_btn_refresh, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(s_btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_refresh = lv_label_create(s_btn_refresh);
    lv_label_set_text(label_refresh, "↻");
    lv_obj_set_style_text_color(label_refresh, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_refresh, &lv_font_montserrat_14, 0);
    lv_obj_center(label_refresh);

    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_set_size(divider, 640, 2);
    lv_obj_set_style_bg_color(divider, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 35);
}

static void create_cpu_module(lv_obj_t *parent)
{
    lv_obj_t *cpu_container = lv_obj_create(parent);
    lv_obj_set_size(cpu_container, 240, 100);
    lv_obj_set_style_bg_color(cpu_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(cpu_container, 0, 0);
    lv_obj_set_style_radius(cpu_container, 0, 0);
    lv_obj_clear_flag(cpu_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cpu_container, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_set_flex_flow(cpu_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cpu_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_meter_cpu = lv_meter_create(cpu_container);
    lv_obj_set_size(s_meter_cpu, 90, 90);

    lv_meter_scale_t *scale_cpu = lv_meter_add_scale(s_meter_cpu);
    lv_meter_set_scale_range(s_meter_cpu, scale_cpu, 0, 100, 240, 150);

    lv_meter_indicator_t *cpu_arc_bg = lv_meter_add_arc(s_meter_cpu, scale_cpu, 6, COLOR_INACTIVE, 0);
    lv_meter_set_indicator_start_value(s_meter_cpu, cpu_arc_bg, 0);
    lv_meter_set_indicator_end_value(s_meter_cpu, cpu_arc_bg, 100);

    s_cpu_arc_val = lv_meter_add_arc(s_meter_cpu, scale_cpu, 6, COLOR_PRIMARY, 0);
    lv_meter_set_indicator_start_value(s_meter_cpu, s_cpu_arc_val, 0);
    lv_meter_set_indicator_end_value(s_meter_cpu, s_cpu_arc_val, 70);

    s_cpu_needle = lv_meter_add_needle_line(s_meter_cpu, scale_cpu, 2, COLOR_PRIMARY, 0);
    lv_meter_set_indicator_value(s_meter_cpu, s_cpu_needle, 70);

    lv_obj_set_style_bg_color(s_meter_cpu, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_meter_cpu, 0, 0);
    lv_obj_set_style_pad_all(s_meter_cpu, 0, 0);

    s_label_cpu_percent = lv_label_create(s_meter_cpu);
    lv_label_set_text(s_label_cpu_percent, "70%");
    lv_obj_set_style_text_color(s_label_cpu_percent, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_label_cpu_percent, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label_cpu_percent, LV_ALIGN_CENTER, 0, 20);

    s_meter_temp = lv_meter_create(cpu_container);
    lv_obj_set_size(s_meter_temp, 90, 90);

    lv_meter_scale_t *scale_temp = lv_meter_add_scale(s_meter_temp);
    lv_meter_set_scale_range(s_meter_temp, scale_temp, 0, 100, 240, 150);

    lv_meter_indicator_t *temp_arc_bg = lv_meter_add_arc(s_meter_temp, scale_temp, 6, COLOR_INACTIVE, 0);
    lv_meter_set_indicator_start_value(s_meter_temp, temp_arc_bg, 0);
    lv_meter_set_indicator_end_value(s_meter_temp, temp_arc_bg, 100);

    s_temp_arc_val = lv_meter_add_arc(s_meter_temp, scale_temp, 6, COLOR_TEMP, 0);
    lv_meter_set_indicator_start_value(s_meter_temp, s_temp_arc_val, 0);
    lv_meter_set_indicator_end_value(s_meter_temp, s_temp_arc_val, 58);

    s_temp_needle = lv_meter_add_needle_line(s_meter_temp, scale_temp, 2, COLOR_TEMP, 0);
    lv_meter_set_indicator_value(s_meter_temp, s_temp_needle, 58);

    lv_obj_set_style_bg_color(s_meter_temp, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_meter_temp, 0, 0);
    lv_obj_set_style_pad_all(s_meter_temp, 0, 0);

    s_label_temp_val = lv_label_create(s_meter_temp);
    lv_label_set_text(s_label_temp_val, "58°C");
    lv_obj_set_style_text_color(s_label_temp_val, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_label_temp_val, &lv_font_montserrat_14, 0);
    lv_obj_align(s_label_temp_val, LV_ALIGN_CENTER, 0, 20);

}

static void create_mem_disk_module(lv_obj_t *parent)
{
    lv_obj_t *md_container = lv_obj_create(parent);
    lv_obj_set_size(md_container, 380, 100);
    lv_obj_set_style_bg_color(md_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(md_container, 0, 0);
    lv_obj_set_style_radius(md_container, 0, 0);
    lv_obj_set_style_pad_all(md_container, 0, 0);
    lv_obj_clear_flag(md_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(md_container, LV_ALIGN_TOP_RIGHT, 0, 37);

    lv_obj_set_flex_flow(md_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(md_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *mem_container = lv_obj_create(md_container);
    lv_obj_set_size(mem_container, LV_PCT(100), LV_PCT(30));
    lv_obj_set_style_bg_color(mem_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(mem_container, 0, 0);
    lv_obj_set_style_radius(mem_container, 0, 0);
    lv_obj_clear_flag(mem_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(mem_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mem_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_mem_title = lv_label_create(mem_container);
    lv_label_set_text(label_mem_title, "MEMORY\n8GB");
    lv_obj_set_style_text_color(label_mem_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_mem_title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(label_mem_title, LV_PCT(20));

    s_bar_mem = lv_bar_create(mem_container);
    lv_obj_set_size(s_bar_mem, LV_PCT(60), 18);
    lv_bar_set_value(s_bar_mem, 72, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_mem, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_mem, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_mem, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_mem, 5, LV_PART_INDICATOR);

    s_label_mem_percent = lv_label_create(mem_container);
    lv_label_set_text(s_label_mem_percent, "72%");
    lv_obj_set_style_text_color(s_label_mem_percent, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_label_mem_percent, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_label_mem_percent, LV_PCT(10));
    lv_obj_set_style_text_align(s_label_mem_percent, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *disk_container = lv_obj_create(md_container);
    lv_obj_set_size(disk_container, LV_PCT(100), LV_PCT(30));
    lv_obj_set_style_bg_color(disk_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(disk_container, 0, 0);
    lv_obj_set_style_radius(disk_container, 0, 0);
    lv_obj_clear_flag(disk_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(disk_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(disk_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label_disk_title = lv_label_create(disk_container);
    lv_label_set_text(label_disk_title, "DISK\n2048GB");
    lv_obj_set_style_text_color(label_disk_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_disk_title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(label_disk_title, LV_PCT(20));

    s_bar_disk = lv_bar_create(disk_container);
    lv_obj_set_size(s_bar_disk, LV_PCT(60), 18);
    lv_bar_set_value(s_bar_disk, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_disk, COLOR_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_disk, COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_disk, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_disk, 5, LV_PART_INDICATOR);

    s_label_disk_percent = lv_label_create(disk_container);
    lv_label_set_text(s_label_disk_percent, "50%");
    lv_obj_set_style_text_color(s_label_disk_percent, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_label_disk_percent, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_label_disk_percent, LV_PCT(10));
    lv_obj_set_style_text_align(s_label_disk_percent, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_label_disk_percent, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_hdd_indicators(lv_obj_t *parent)
{
    lv_obj_t *hdd_container = lv_obj_create(parent);
    lv_obj_set_size(hdd_container, 640, 35);
    lv_obj_set_style_bg_color(hdd_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(hdd_container, 0, 0);
    lv_obj_set_style_radius(hdd_container, 0, 0);
    lv_obj_clear_flag(hdd_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdd_container, LV_ALIGN_BOTTOM_MID, 0, -5);

    const char *hdd_labels[] = {"HDD1", "HDD2", "HDD3", "HDD4", "HDD5", "HDD6"};
    const bool hdd_active[] = {false, true, true, true, true, true};

    for (int i = 0; i < 6; i++) {
        lv_obj_t *btn_hdd = lv_btn_create(hdd_container);
        lv_obj_set_size(btn_hdd, 95, 35);
        lv_obj_set_style_bg_color(btn_hdd, COLOR_INACTIVE, 0);
        lv_obj_set_style_border_width(btn_hdd, 0, 0);
        lv_obj_set_style_radius(btn_hdd, 3, 0);
        lv_obj_set_style_pad_all(btn_hdd, 0, 0);
        lv_obj_align(btn_hdd, LV_ALIGN_LEFT_MID, 8 + i * 103, 0);

        lv_obj_add_event_cb(btn_hdd, ui_event_Screen_Overview_hdd_clicked, LV_EVENT_ALL, NULL);

        lv_obj_t *label_hdd = lv_label_create(btn_hdd);
        lv_label_set_text(label_hdd, hdd_labels[i]);
        lv_obj_set_style_text_color(label_hdd, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(label_hdd, &lv_font_montserrat_14, 0);
        lv_obj_center(label_hdd);

        lv_obj_t *led = lv_obj_create(btn_hdd);
        lv_obj_set_size(led, 14, 14);
        lv_obj_set_style_bg_color(led, hdd_active[i] ? COLOR_LED_GREEN : COLOR_LED_GRAY, 0);
        lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(led, LV_ALIGN_RIGHT_MID, -5, 0);
    }
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_label_time != NULL) {
        time_t now_t = time(NULL);
        struct tm *tm_now = localtime(&now_t);
        static char time_str[9];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
        lv_label_set_text(s_label_time, time_str);
    }

    if (s_label_up != NULL) {
        float tx_speed = data_source_get_tx_speed_mbps();
        static char tx_str[16];

        if (tx_speed < 0.01f) {
            snprintf(tx_str, sizeof(tx_str), "▲ 0.00KB/s");
        } else if (tx_speed < 1.0f) {
            snprintf(tx_str, sizeof(tx_str), "▲ %.2fKB/s", tx_speed * 1024.0f);
        } else {
            snprintf(tx_str, sizeof(tx_str), "▲ %.2fMB/s", tx_speed);
        }
        lv_label_set_text(s_label_up, tx_str);
    }

    if (s_label_down != NULL) {
        float rx_speed = data_source_get_rx_speed_mbps();
        static char rx_str[16];

        if (rx_speed < 0.01f) {
            snprintf(rx_str, sizeof(rx_str), "▼ 0.00KB/s");
        } else if (rx_speed < 1.0f) {
            snprintf(rx_str, sizeof(rx_str), "▼ %.2fKB/s", rx_speed * 1024.0f);
        } else {
            snprintf(rx_str, sizeof(rx_str), "▼ %.2fMB/s", rx_speed);
        }
        lv_label_set_text(s_label_down, rx_str);
    }

    if (s_label_ip != NULL) {
        const char *ip = g_config.nas_ip;
        if (ip && ip[0]) {
            static char ip_str[48];
            snprintf(ip_str, sizeof(ip_str), "IP: %s", ip);
            lv_label_set_text(s_label_ip, ip_str);
        } else {
            lv_label_set_text(s_label_ip, "IP: --");
        }
    }

    if (s_icon_wifi != NULL) {
        char ssid_buf[33];
        wifi_get_curr_ssid(ssid_buf, sizeof(ssid_buf));
        if (wifi_is_connected()) {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0x80, 0xff, 0x80), 0);
        } else if (ssid_buf[0]) {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0xff, 0xa0, 0x40), 0);
        } else {
            lv_obj_set_style_text_color(s_icon_wifi, lv_color_make(0x40, 0x40, 0x40), 0);
        }
    }

    if (s_icon_bt != NULL) {
        lv_obj_set_style_text_color(s_icon_bt, lv_color_make(0x40, 0x40, 0x40), 0);
    }

    const NasData *data = data_source_get_data();
    if (data && data->is_online) {
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
    }
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGD("Overview", "Refresh button clicked");
    event_bus_publish(EVENT_TRIGGER_HTTP_FETCH, NULL, 0);
}

void ui_Screen_Overview_screen_init(void)
{
    if (ui_Screen_Overview != NULL) {
        ESP_LOGW("Overview", "Screen already initialized, destroying first");
        ui_Screen_Overview_screen_destroy();
    }

    ui_Screen_Overview = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen_Overview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_Overview, COLOR_BG, 0);

    create_status_bar(ui_Screen_Overview);
    create_cpu_module(ui_Screen_Overview);
    create_mem_disk_module(ui_Screen_Overview);
    create_hdd_indicators(ui_Screen_Overview);

    lv_obj_add_event_cb(ui_Screen_Overview, ui_event_Screen_Overview_gesture, LV_EVENT_GESTURE, NULL);

    s_update_timer = lv_timer_create(update_timer_cb, 5000, NULL);
    ESP_LOGI("Overview", "Overview screen initialized");
}

void ui_Screen_Overview_screen_destroy(void)
{
    if (s_update_timer) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
    }

    if (ui_Screen_Overview) {
        lv_obj_del(ui_Screen_Overview);
        ui_Screen_Overview = NULL;
    }

    s_label_time = NULL;
    s_label_up = NULL;
    s_label_down = NULL;
    s_label_ip = NULL;
    s_btn_refresh = NULL;
    s_icon_wifi = NULL;
    s_icon_bt = NULL;

    s_meter_cpu = NULL;
    s_meter_temp = NULL;
    s_label_cpu_percent = NULL;
    s_label_temp_val = NULL;
    s_cpu_arc_val = NULL;
    s_cpu_needle = NULL;
    s_temp_arc_val = NULL;
    s_temp_needle = NULL;

    s_bar_mem = NULL;
    s_label_mem_percent = NULL;
    s_bar_disk = NULL;
    s_label_disk_percent = NULL;

    ESP_LOGI("Overview", "Overview screen destroyed");
}