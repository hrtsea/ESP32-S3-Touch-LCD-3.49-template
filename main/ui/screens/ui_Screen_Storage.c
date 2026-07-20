#include "../ui.h"
#include "ui_Screen_Storage.h"
#include "esp_log.h"
#include "esp_wifi_config.h"
#include "wifi_adapter.h"

LV_FONT_DECLARE(lv_font_montserrat_12);

lv_obj_t *ui_Screen_Storage = NULL;

#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_PRIMARY   lv_color_hex(0x40E0D0)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_INACTIVE  lv_color_hex(0x333333)
#define COLOR_WARNING   lv_color_make(0xff, 0xa0, 0x40)
#define COLOR_CRITICAL  lv_color_make(0xff, 0x40, 0x40)

#define LV_OBJ_CHECK(obj, name) \
    if (!(obj)) { \
        ESP_LOGE("Storage", "Failed to create %s - out of memory", name); \
        return; \
    }

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *label_time;
    lv_obj_t *label_up;
    lv_obj_t *label_down;
    lv_obj_t *label_ip;
    lv_obj_t *container;
    lv_obj_t *hdd_names[MAX_DISKS];
    lv_obj_t *hdd_bars[MAX_DISKS];
    lv_obj_t *hdd_percents[MAX_DISKS];
    lv_obj_t *hdd_temps[MAX_DISKS];
} StorageScreen;

static StorageScreen s_screen = {0};

static void create_storage_status_bar(lv_obj_t *parent)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    LV_OBJ_CHECK(status_bar, "status_bar");

    lv_obj_set_size(status_bar, 640, 30);
    lv_obj_set_style_bg_color(status_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_title, "label_title");
    lv_label_set_text(label_title, "Storage");
    lv_obj_set_style_text_color(label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_12, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *label_time = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_time, "label_time");
    s_screen.label_time = label_time;
    lv_label_set_text(label_time, "--:--");
    lv_obj_set_style_text_color(label_time, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_12, 0);
    lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 80, 0);

    lv_obj_t *label_up = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_up, "label_up");
    s_screen.label_up = label_up;
    lv_label_set_text(label_up, "▲ 0KB/s");
    lv_obj_set_style_text_color(label_up, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_up, &lv_font_montserrat_12, 0);
    lv_obj_align(label_up, LV_ALIGN_CENTER, -70, 0);

    lv_obj_t *label_down = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_down, "label_down");
    s_screen.label_down = label_down;
    lv_label_set_text(label_down, "▼ 0KB/s");
    lv_obj_set_style_text_color(label_down, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_down, &lv_font_montserrat_12, 0);
    lv_obj_align(label_down, LV_ALIGN_CENTER, 70, 0);

    lv_obj_t *label_ip = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_ip, "label_ip");
    s_screen.label_ip = label_ip;
    lv_label_set_text(label_ip, "IP: --");
    lv_obj_set_style_text_color(label_ip, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_ip, &lv_font_montserrat_12, 0);
    lv_obj_align(label_ip, LV_ALIGN_RIGHT_MID, -15, 0);

    lv_obj_t *divider = lv_obj_create(parent);
    LV_OBJ_CHECK(divider, "divider");
    lv_obj_set_size(divider, 640, 1);
    lv_obj_set_style_bg_color(divider, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 30);
}

static void create_hdd_storage_bars(lv_obj_t *parent)
{
    s_screen.container = lv_obj_create(parent);
    LV_OBJ_CHECK(s_screen.container, "storage_container");

    lv_obj_set_size(s_screen.container, 640, 140);
    lv_obj_set_style_bg_color(s_screen.container, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_screen.container, 0, 0);
    lv_obj_set_style_radius(s_screen.container, 0, 0);
    lv_obj_clear_flag(s_screen.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_screen.container, LV_ALIGN_TOP_MID, 0, 32);

    uint8_t total_disks = config_get_total_disk_slots();
    if (total_disks == 0) total_disks = 1;

    uint8_t cols = (total_disks <= 4) ? 1 : (total_disks <= 6) ? 2 : 3;
    uint8_t rows = (total_disks + cols - 1) / cols;

    int col_width = (cols == 1) ? 620 : (cols == 2) ? 310 : 205;
    int row_height = 22;

    for (uint8_t col = 0; col < cols; col++) {
        for (uint8_t row = 0; row < rows; row++) {
            uint8_t index = col * rows + row;
            if (index >= total_disks) break;

            int x_offset = col * col_width + 8;
            int y_offset = row * row_height + 2;

            char label_text[16];
            if (config_is_sata_slot(index)) {
                snprintf(label_text, sizeof(label_text), "HDD%d", index + 1);
            } else {
                uint8_t m2_index = index - g_config.sata_disk_count;
                snprintf(label_text, sizeof(label_text), "M.2%d", m2_index + 1);
            }

            s_screen.hdd_names[index] = lv_label_create(s_screen.container);
            LV_OBJ_CHECK(s_screen.hdd_names[index], "hdd_name");
            lv_label_set_text(s_screen.hdd_names[index], label_text);
            lv_obj_set_style_text_color(s_screen.hdd_names[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_screen.hdd_names[index], &lv_font_montserrat_12, 0);
            lv_obj_align(s_screen.hdd_names[index], LV_ALIGN_TOP_LEFT, x_offset, y_offset);

            int bar_width = (cols == 1) ? 400 : (cols == 2) ? 160 : 100;
            s_screen.hdd_bars[index] = lv_bar_create(s_screen.container);
            LV_OBJ_CHECK(s_screen.hdd_bars[index], "hdd_bar");
            lv_obj_set_size(s_screen.hdd_bars[index], bar_width, 12);
            lv_bar_set_value(s_screen.hdd_bars[index], 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_screen.hdd_bars[index], COLOR_INACTIVE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_screen.hdd_bars[index], COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_obj_set_style_radius(s_screen.hdd_bars[index], 3, LV_PART_MAIN);
            lv_obj_set_style_radius(s_screen.hdd_bars[index], 3, LV_PART_INDICATOR);
            lv_obj_align(s_screen.hdd_bars[index], LV_ALIGN_TOP_LEFT, x_offset + 50, y_offset + 2);

            s_screen.hdd_percents[index] = lv_label_create(s_screen.container);
            LV_OBJ_CHECK(s_screen.hdd_percents[index], "hdd_percent");
            lv_label_set_text(s_screen.hdd_percents[index], "0%");
            lv_obj_set_style_text_color(s_screen.hdd_percents[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_screen.hdd_percents[index], &lv_font_montserrat_12, 0);
            lv_obj_align(s_screen.hdd_percents[index], LV_ALIGN_TOP_LEFT, x_offset + 50 + bar_width + 4, y_offset);

            s_screen.hdd_temps[index] = lv_label_create(s_screen.container);
            LV_OBJ_CHECK(s_screen.hdd_temps[index], "hdd_temp");
            lv_label_set_text(s_screen.hdd_temps[index], "--°C");
            lv_obj_set_style_text_color(s_screen.hdd_temps[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_screen.hdd_temps[index], &lv_font_montserrat_12, 0);
            lv_obj_align(s_screen.hdd_temps[index], LV_ALIGN_TOP_LEFT, x_offset + 50 + bar_width + 30, y_offset);
        }
    }
}

void ui_Screen_Storage_screen_init(void)
{
    if (ui_Screen_Storage != NULL) {
        ESP_LOGW("Storage", "Screen already initialized, destroying first");
        ui_Screen_Storage_screen_destroy();
    }

    ui_Screen_Storage = lv_obj_create(NULL);
    LV_OBJ_CHECK(ui_Screen_Storage, "ui_Screen_Storage screen");

    s_screen.screen = ui_Screen_Storage;

    lv_obj_set_size(ui_Screen_Storage, 640, 172);
    lv_obj_clear_flag(ui_Screen_Storage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_Storage, COLOR_BG, 0);
    lv_obj_add_flag(ui_Screen_Storage, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_storage_status_bar(ui_Screen_Storage);
    create_hdd_storage_bars(ui_Screen_Storage);

    lv_obj_add_event_cb(ui_Screen_Storage, ui_event_Screen_Storage_gesture, LV_EVENT_GESTURE, NULL);

    ESP_LOGI("Storage", "Screen initialized successfully");
}

void ui_Screen_Storage_screen_destroy(void)
{
    s_screen.label_time = NULL;
    s_screen.label_up = NULL;
    s_screen.label_down = NULL;
    s_screen.label_ip = NULL;
    s_screen.container = NULL;

    for (int i = 0; i < MAX_DISKS; i++) {
        s_screen.hdd_names[i] = NULL;
        s_screen.hdd_bars[i] = NULL;
        s_screen.hdd_percents[i] = NULL;
        s_screen.hdd_temps[i] = NULL;
    }
    s_screen.screen = NULL;

    if (ui_Screen_Storage) {
        lv_obj_del(ui_Screen_Storage);
        ui_Screen_Storage = NULL;
        ESP_LOGI("Storage", "Screen destroyed successfully");
    }
}

void storage_screen_update_time(const char *time_str)
{
    if (s_screen.label_time) {
        lv_label_set_text(s_screen.label_time, time_str);
    }
}

void storage_screen_update_network(int upload_kbps, int download_kbps)
{
    if (s_screen.label_up) {
        static char tx_str[16];
        float tx_speed = (float)upload_kbps / 1024.0f;
        if (tx_speed < 0.01f) {
            snprintf(tx_str, sizeof(tx_str), "▲ 0KB/s");
        } else if (tx_speed < 1.0f) {
            snprintf(tx_str, sizeof(tx_str), "▲ %.0fKB/s", tx_speed * 1024.0f);
        } else {
            snprintf(tx_str, sizeof(tx_str), "▲ %.1fMB/s", tx_speed);
        }
        lv_label_set_text(s_screen.label_up, tx_str);
    }
    if (s_screen.label_down) {
        static char rx_str[16];
        float rx_speed = (float)download_kbps / 1024.0f;
        if (rx_speed < 0.01f) {
            snprintf(rx_str, sizeof(rx_str), "▼ 0KB/s");
        } else if (rx_speed < 1.0f) {
            snprintf(rx_str, sizeof(rx_str), "▼ %.0fKB/s", rx_speed * 1024.0f);
        } else {
            snprintf(rx_str, sizeof(rx_str), "▼ %.1fMB/s", rx_speed);
        }
        lv_label_set_text(s_screen.label_down, rx_str);
    }
}

void storage_screen_update_ip(const char *ip_str)
{
    if (s_screen.label_ip) {
        static char ip_buf[24];
        if (ip_str && ip_str[0]) {
            snprintf(ip_buf, sizeof(ip_buf), "IP: %s", ip_str);
        } else {
            snprintf(ip_buf, sizeof(ip_buf), "IP: --");
        }
        lv_label_set_text(s_screen.label_ip, ip_buf);
    }
}

void storage_screen_update_hdd_name(int index, const char *name)
{
    if (index >= 0 && index < MAX_DISKS && s_screen.hdd_names[index]) {
        const char *display_name = name && name[0] ? name : "HDD";
        lv_label_set_text(s_screen.hdd_names[index], display_name);
    }
}

void storage_screen_update_hdd_bar(int index, int used_pct, int health)
{
    if (index >= 0 && index < MAX_DISKS && s_screen.hdd_bars[index]) {
        used_pct = (used_pct < 0) ? 0 : (used_pct > 100) ? 100 : used_pct;
        lv_bar_set_value(s_screen.hdd_bars[index], used_pct, LV_ANIM_ON);

        lv_color_t color = COLOR_PRIMARY;
        if (health == 2) {
            color = COLOR_WARNING;
        } else if (health == 3) {
            color = COLOR_CRITICAL;
        }
        lv_obj_set_style_bg_color(s_screen.hdd_bars[index], color, LV_PART_INDICATOR);
    }
    if (index >= 0 && index < MAX_DISKS && s_screen.hdd_percents[index]) {
        used_pct = (used_pct < 0) ? 0 : (used_pct > 100) ? 100 : used_pct;
        static char pct_str[8];
        snprintf(pct_str, sizeof(pct_str), "%d%%", used_pct);
        lv_label_set_text(s_screen.hdd_percents[index], pct_str);
    }
}

void storage_screen_update_hdd_temp(int index, int temp)
{
    if (index >= 0 && index < MAX_DISKS && s_screen.hdd_temps[index]) {
        static char temp_str[16];
        if (temp > 0) {
            snprintf(temp_str, sizeof(temp_str), "%d°C", temp);
        } else {
            snprintf(temp_str, sizeof(temp_str), "--°C");
        }
        lv_label_set_text(s_screen.hdd_temps[index], temp_str);
    }
}

void storage_screen_update_hdd_online(int index, bool online)
{
    if (index >= 0 && index < MAX_DISKS) {
        if (s_screen.hdd_names[index]) {
            if (online) {
                lv_obj_clear_flag(s_screen.hdd_names[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_screen.hdd_names[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_screen.hdd_bars[index]) {
            if (online) {
                lv_obj_clear_flag(s_screen.hdd_bars[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_screen.hdd_bars[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_screen.hdd_percents[index]) {
            if (online) {
                lv_obj_clear_flag(s_screen.hdd_percents[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_screen.hdd_percents[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_screen.hdd_temps[index]) {
            if (online) {
                lv_obj_clear_flag(s_screen.hdd_temps[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_screen.hdd_temps[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}