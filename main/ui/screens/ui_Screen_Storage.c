#include "ui_Screen_Storage.h"
#include "../ui_events.h"
#include "esp_log.h"
#include <stdio.h>

lv_obj_t * ui_Screen_Storage = NULL;

#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_PRIMARY   lv_color_hex(0x40E0D0)
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_INACTIVE  lv_color_hex(0x333333)

#define HDD_COUNT 8

#define LV_OBJ_CHECK(obj, name) \
    if (!(obj)) { \
        ESP_LOGE("Storage", "Failed to create %s - out of memory", name); \
        return; \
    }

static void create_storage_status_bar(lv_obj_t *parent)
{
    lv_obj_t *status_bar = lv_obj_create(parent);
    LV_OBJ_CHECK(status_bar, "status_bar");

    lv_obj_set_size(status_bar, 640, 35);
    lv_obj_set_style_bg_color(status_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_title = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_title, "label_title");
    lv_label_set_text(label_title, "BOOL NAS");
    lv_obj_set_style_text_color(label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *label_time = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_time, "label_time");
    lv_label_set_text(label_time, "22:10");
    lv_obj_set_style_text_color(label_time, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
    lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 110, 0);

    lv_obj_t *label_up = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_up, "label_up");
    lv_label_set_text(label_up, "▲ 0.91MB/s");
    lv_obj_set_style_text_color(label_up, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_up, &lv_font_montserrat_14, 0);
    lv_obj_align(label_up, LV_ALIGN_CENTER, -70, 0);

    lv_obj_t *label_down = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_down, "label_down");
    lv_label_set_text(label_down, "▼ 5.20MB/s");
    lv_obj_set_style_text_color(label_down, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_down, &lv_font_montserrat_14, 0);
    lv_obj_align(label_down, LV_ALIGN_CENTER, 70, 0);

    lv_obj_t *label_ip = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_ip, "label_ip");
    lv_label_set_text(label_ip, "IP: 192.168.1.888");
    lv_obj_set_style_text_color(label_ip, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_ip, &lv_font_montserrat_14, 0);
    lv_obj_align(label_ip, LV_ALIGN_RIGHT_MID, -15, 0);

    lv_obj_t *divider = lv_obj_create(parent);
    LV_OBJ_CHECK(divider, "divider");
    lv_obj_set_size(divider, 640, 2);
    lv_obj_set_style_bg_color(divider, COLOR_INACTIVE, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 35);
}

static void create_hdd_storage_bars(lv_obj_t *parent)
{
    ESP_LOGI("Storage", "Creating storage container...");
    lv_obj_t *storage_container = lv_obj_create(parent);
    LV_OBJ_CHECK(storage_container, "storage_container");

    ESP_LOGI("Storage", "storage_container = %p", storage_container);
    lv_obj_set_size(storage_container, 640, 130);
    lv_obj_set_style_bg_color(storage_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(storage_container, 0, 0);
    lv_obj_set_style_radius(storage_container, 0, 0);
    lv_obj_clear_flag(storage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(storage_container, LV_ALIGN_TOP_MID, 0, 40);

    const char *hdd_names[HDD_COUNT] = {"HDD1", "HDD2", "HDD3", "HDD4", "HDD5", "HDD6", "M.2-1", "M.2-2"};
    const int hdd_usage[HDD_COUNT] = {35, 68, 82, 10, 23, 56, 71, 69};
    const bool hdd_active[HDD_COUNT] = {true, true, true, true, true, true, true, true};

    for (int col = 0; col < 2; col++) {
        for (int row = 0; row < 4; row++) {
            int index = col * 4 + row;
            if (index >= HDD_COUNT) break;

            ESP_LOGI("Storage", "Creating HDD %d (col=%d, row=%d)", index, col, row);

            int x_offset = col * 310 + 10;
            int y_offset = row * 28 + 5;

            lv_obj_t *label_name = lv_label_create(storage_container);
            LV_OBJ_CHECK(label_name, "label_name");
            lv_label_set_text(label_name, hdd_names[index]);
            lv_obj_set_style_text_color(label_name, COLOR_TEXT, 0);
            lv_obj_set_style_text_font(label_name, &lv_font_montserrat_14, 0);
            lv_obj_align(label_name, LV_ALIGN_TOP_LEFT, x_offset, y_offset);

            lv_obj_t *bar_hdd = lv_bar_create(storage_container);
            LV_OBJ_CHECK(bar_hdd, "bar_hdd");
            lv_obj_set_size(bar_hdd, 170, 16);
            lv_bar_set_value(bar_hdd, hdd_usage[index], LV_ANIM_OFF);
            lv_obj_set_style_bg_color(bar_hdd, COLOR_INACTIVE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar_hdd, COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_obj_set_style_radius(bar_hdd, 4, LV_PART_MAIN);
            lv_obj_set_style_radius(bar_hdd, 4, LV_PART_INDICATOR);
            lv_obj_align(bar_hdd, LV_ALIGN_TOP_LEFT, x_offset + 50, y_offset);

            char percent_text[8];
            snprintf(percent_text, sizeof(percent_text), "%d%%", hdd_usage[index]);
            lv_obj_t *label_percent = lv_label_create(storage_container);
            LV_OBJ_CHECK(label_percent, "label_percent");
            lv_label_set_text(label_percent, percent_text);
            lv_obj_set_style_text_color(label_percent, COLOR_TEXT, 0);
            lv_obj_set_style_text_font(label_percent, &lv_font_montserrat_14, 0);
            lv_obj_align(label_percent, LV_ALIGN_TOP_LEFT, x_offset + 230, y_offset);
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

    lv_obj_clear_flag(ui_Screen_Storage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_Storage, COLOR_BG, 0);

    create_storage_status_bar(ui_Screen_Storage);
    create_hdd_storage_bars(ui_Screen_Storage);

    lv_obj_add_event_cb(ui_Screen_Storage, ui_event_Screen_Storage_gesture, LV_EVENT_GESTURE, NULL);

    ESP_LOGI("Storage", "Screen initialized successfully");
}

void ui_Screen_Storage_screen_destroy(void)
{
    if(ui_Screen_Storage) {
        lv_obj_del(ui_Screen_Storage);
        ui_Screen_Storage = NULL;
        ESP_LOGI("Storage", "Screen destroyed successfully");
    }
}