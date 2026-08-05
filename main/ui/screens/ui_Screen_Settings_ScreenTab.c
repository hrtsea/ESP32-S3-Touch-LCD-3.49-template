#include "../ui.h"
#include "ui_Screen_Settings_ScreenTab.h"
#include "theme.h"
#include "../../config/app_cfg.h"
#include "esp_log.h"

static const char *TAG = "ScreenTab";

lv_obj_t * ui_Settings_Tabpage_screen = NULL;
lv_obj_t * ui_Settings_Dropdown_Brightness = NULL;
lv_obj_t * ui_Settings_Dropdown_SleepTimer = NULL;
lv_obj_t * ui_Settings_Dropdown_Wallpaper = NULL;
lv_obj_t * ui_Settings_Dropdown_Theme = NULL;
lv_obj_t * ui_Settings_Label_screenHints = NULL;

static void ui_event_Settings_Tabpage_screen(lv_event_t * e);
static void ui_event_Settings_Dropdown_Brightness(lv_event_t * e);
static void ui_event_Settings_Dropdown_SleepTimer(lv_event_t * e);
static void ui_event_Settings_Dropdown_Wallpaper(lv_event_t * e);
static void ui_event_Settings_Dropdown_Theme(lv_event_t * e);

void ui_event_Settings_Tabpage_screen(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Dropdown_Brightness(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setBrightness(e);
    }
}

void ui_event_Settings_Dropdown_SleepTimer(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setTimer(e);
    }
}

void ui_event_Settings_Dropdown_Wallpaper(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setWallpaper(e);
    }
}

void ui_event_Settings_Dropdown_Theme(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *dropdown = lv_event_get_target(e);
        uint16_t sel = lv_dropdown_get_selected(dropdown);
        ESP_LOGI(TAG, "Theme changed to: %d", sel);
        app_cfg_set_theme(sel);

        /* 重新初始化当前屏幕以应用主题 */
        if (ui_Screen_Settings != NULL) {
            ui_Screen_Settings_screen_destroy();
            ui_Screen_Settings_screen_init();
        }
    }
}

void ui_Screen_Settings_ScreenTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_screen = lv_tabview_add_tab(parent, "Screen");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_screen, LV_DIR_HOR);

    ui_Settings_Dropdown_Brightness = lv_dropdown_create(ui_Settings_Tabpage_screen);
    lv_dropdown_set_options(ui_Settings_Dropdown_Brightness, "LOW\nMEDIUM\nHIGH");
    lv_obj_set_width(ui_Settings_Dropdown_Brightness, 120);
    lv_obj_set_height(ui_Settings_Dropdown_Brightness, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Dropdown_Brightness, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_flag(ui_Settings_Dropdown_Brightness, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_Settings_Dropdown_SleepTimer = lv_dropdown_create(ui_Settings_Tabpage_screen);
    lv_dropdown_set_options(ui_Settings_Dropdown_SleepTimer, "0\n15\n30\n60");
    lv_obj_set_width(ui_Settings_Dropdown_SleepTimer, 120);
    lv_obj_set_height(ui_Settings_Dropdown_SleepTimer, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Dropdown_SleepTimer, LV_ALIGN_CENTER, 130, 15);
    lv_obj_add_flag(ui_Settings_Dropdown_SleepTimer, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_Settings_Dropdown_Wallpaper = lv_dropdown_create(ui_Settings_Tabpage_screen);
    lv_dropdown_set_options(ui_Settings_Dropdown_Wallpaper, "");
    lv_obj_set_width(ui_Settings_Dropdown_Wallpaper, 120);
    lv_obj_set_height(ui_Settings_Dropdown_Wallpaper, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Dropdown_Wallpaper, LV_ALIGN_CENTER, 260, 15);
    lv_obj_add_flag(ui_Settings_Dropdown_Wallpaper, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_Settings_Dropdown_Theme = lv_dropdown_create(ui_Settings_Tabpage_screen);
    lv_dropdown_set_options(ui_Settings_Dropdown_Theme, "Dark\nLight\nYellow");
    lv_obj_set_width(ui_Settings_Dropdown_Theme, 120);
    lv_obj_set_height(ui_Settings_Dropdown_Theme, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Dropdown_Theme, LV_ALIGN_CENTER, 390, 15);
    lv_obj_add_flag(ui_Settings_Dropdown_Theme, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_dropdown_set_selected(ui_Settings_Dropdown_Theme, g_cfg.theme);

    ui_Settings_Label_screenHints = lv_label_create(ui_Settings_Tabpage_screen);
    lv_obj_set_width(ui_Settings_Label_screenHints, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_screenHints, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_screenHints, LV_ALIGN_CENTER, 15, -15);
    lv_label_set_text(ui_Settings_Label_screenHints, "Brightness  Sleep timer  Wallpaper  Theme");

    lv_obj_add_event_cb(ui_Settings_Dropdown_Brightness, ui_event_Settings_Dropdown_Brightness, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Dropdown_SleepTimer, ui_event_Settings_Dropdown_SleepTimer, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Dropdown_Wallpaper, ui_event_Settings_Dropdown_Wallpaper, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Dropdown_Theme, ui_event_Settings_Dropdown_Theme, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_screen, ui_event_Settings_Tabpage_screen, LV_EVENT_ALL, NULL);
}

void ui_Screen_Settings_ScreenTab_cleanup(void)
{
    ui_Settings_Tabpage_screen = NULL;
    ui_Settings_Dropdown_Brightness = NULL;
    ui_Settings_Dropdown_SleepTimer = NULL;
    ui_Settings_Dropdown_Wallpaper = NULL;
    ui_Settings_Dropdown_Theme = NULL;
    ui_Settings_Label_screenHints = NULL;
}
