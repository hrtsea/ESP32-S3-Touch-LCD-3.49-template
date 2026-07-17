#include "../ui.h"
#include "ui_Screen_Storage.h"
#include "esp_log.h"
#include "esp_wifi_config.h"
#include "wifi_adapter.h"

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

lv_obj_t *s_storage_label_time = NULL;
lv_obj_t *s_storage_label_up = NULL;
lv_obj_t *s_storage_label_down = NULL;
lv_obj_t *s_storage_label_ip = NULL;

lv_obj_t *s_hdd_names[MAX_DISKS] = {NULL};
lv_obj_t *s_hdd_bars[MAX_DISKS] = {NULL};
lv_obj_t *s_hdd_percents[MAX_DISKS] = {NULL};
lv_obj_t *s_hdd_temps[MAX_DISKS] = {NULL};
static lv_obj_t *s_storage_container = NULL;

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
    lv_label_set_text(label_title, "Storage");
    lv_obj_set_style_text_color(label_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *label_time = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_time, "label_time");
    s_storage_label_time = label_time;
    lv_label_set_text(label_time, "--:--");
    lv_obj_set_style_text_color(label_time, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);
    lv_obj_align(label_time, LV_ALIGN_LEFT_MID, 110, 0);

    lv_obj_t *label_up = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_up, "label_up");
    s_storage_label_up = label_up;
    lv_label_set_text(label_up, "▲ 0.00KB/s");
    lv_obj_set_style_text_color(label_up, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_up, &lv_font_montserrat_14, 0);
    lv_obj_align(label_up, LV_ALIGN_CENTER, -70, 0);

    lv_obj_t *label_down = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_down, "label_down");
    s_storage_label_down = label_down;
    lv_label_set_text(label_down, "▼ 0.00KB/s");
    lv_obj_set_style_text_color(label_down, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_down, &lv_font_montserrat_14, 0);
    lv_obj_align(label_down, LV_ALIGN_CENTER, 70, 0);

    lv_obj_t *label_ip = lv_label_create(status_bar);
    LV_OBJ_CHECK(label_ip, "label_ip");
    s_storage_label_ip = label_ip;
    lv_label_set_text(label_ip, "IP: --");
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
    s_storage_container = lv_obj_create(parent);
    LV_OBJ_CHECK(s_storage_container, "storage_container");

    lv_obj_set_size(s_storage_container, 640, 130);
    lv_obj_set_style_bg_color(s_storage_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(s_storage_container, 0, 0);
    lv_obj_set_style_radius(s_storage_container, 0, 0);
    lv_obj_clear_flag(s_storage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_storage_container, LV_ALIGN_TOP_MID, 0, 40);

    uint8_t total_disks = config_get_total_disk_slots();
    if (total_disks == 0) total_disks = 1;

    uint8_t cols = (total_disks <= 4) ? 1 : 2;
    uint8_t rows = (total_disks + cols - 1) / cols;

    for (uint8_t col = 0; col < cols; col++) {
        for (uint8_t row = 0; row < rows; row++) {
            uint8_t index = col * rows + row;
            if (index >= total_disks) break;

            int x_offset = col * 310 + 10;
            int y_offset = row * 28 + 5;

            char label_text[16];
            if (config_is_sata_slot(index)) {
                snprintf(label_text, sizeof(label_text), "HDD%d", index + 1);
            } else {
                uint8_t m2_index = index - g_config.sata_disk_count;
                snprintf(label_text, sizeof(label_text), "M.2%d", m2_index + 1);
            }

            s_hdd_names[index] = lv_label_create(s_storage_container);
            LV_OBJ_CHECK(s_hdd_names[index], "hdd_name");
            lv_label_set_text(s_hdd_names[index], label_text);
            lv_obj_set_style_text_color(s_hdd_names[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_hdd_names[index], &lv_font_montserrat_14, 0);
            lv_obj_align(s_hdd_names[index], LV_ALIGN_TOP_LEFT, x_offset, y_offset);

            s_hdd_bars[index] = lv_bar_create(s_storage_container);
            LV_OBJ_CHECK(s_hdd_bars[index], "hdd_bar");
            lv_obj_set_size(s_hdd_bars[index], 170, 16);
            lv_bar_set_value(s_hdd_bars[index], 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_hdd_bars[index], COLOR_INACTIVE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_hdd_bars[index], COLOR_PRIMARY, LV_PART_INDICATOR);
            lv_obj_set_style_radius(s_hdd_bars[index], 4, LV_PART_MAIN);
            lv_obj_set_style_radius(s_hdd_bars[index], 4, LV_PART_INDICATOR);
            lv_obj_align(s_hdd_bars[index], LV_ALIGN_TOP_LEFT, x_offset + 50, y_offset);

            s_hdd_percents[index] = lv_label_create(s_storage_container);
            LV_OBJ_CHECK(s_hdd_percents[index], "hdd_percent");
            lv_label_set_text(s_hdd_percents[index], "0%");
            lv_obj_set_style_text_color(s_hdd_percents[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_hdd_percents[index], &lv_font_montserrat_14, 0);
            lv_obj_align(s_hdd_percents[index], LV_ALIGN_TOP_LEFT, x_offset + 230, y_offset);

            s_hdd_temps[index] = lv_label_create(s_storage_container);
            LV_OBJ_CHECK(s_hdd_temps[index], "hdd_temp");
            lv_label_set_text(s_hdd_temps[index], "--°C");
            lv_obj_set_style_text_color(s_hdd_temps[index], COLOR_TEXT, 0);
            lv_obj_set_style_text_font(s_hdd_temps[index], &lv_font_montserrat_14, 0);
            lv_obj_align(s_hdd_temps[index], LV_ALIGN_TOP_LEFT, x_offset + 265, y_offset);
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
    lv_obj_add_flag(ui_Screen_Storage, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_storage_status_bar(ui_Screen_Storage);
    create_hdd_storage_bars(ui_Screen_Storage);

    lv_obj_add_event_cb(ui_Screen_Storage, ui_event_Screen_Storage_gesture, LV_EVENT_GESTURE, NULL);

    ESP_LOGI("Storage", "Screen initialized successfully");
}

void ui_Screen_Storage_screen_destroy(void)
{
    s_storage_label_time = NULL;
    s_storage_label_up = NULL;
    s_storage_label_down = NULL;
    s_storage_label_ip = NULL;

    for (int i = 0; i < MAX_DISKS; i++) {
        s_hdd_names[i] = NULL;
        s_hdd_bars[i] = NULL;
        s_hdd_percents[i] = NULL;
        s_hdd_temps[i] = NULL;
    }
    s_storage_container = NULL;

    if (ui_Screen_Storage) {
        lv_obj_del(ui_Screen_Storage);
        ui_Screen_Storage = NULL;
        ESP_LOGI("Storage", "Screen destroyed successfully");
    }
}