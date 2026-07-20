#include "../ui.h"
#include "esp_log.h"

LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_32);

static const char *TAG = "ui_Screen_Settings";

lv_style_t style_btn_border;
lv_style_t style_tab_bg;
static bool styles_inited = false;

static void init_styles(void)
{
    if (styles_inited) return;

    lv_style_init(&style_btn_border);
    lv_style_set_bg_color(&style_btn_border, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_btn_border, 0);
    lv_style_set_border_color(&style_btn_border, lv_color_hex(0xFFFFFF));
    lv_style_set_border_opa(&style_btn_border, 255);
    lv_style_set_border_width(&style_btn_border, 3);

    lv_style_init(&style_tab_bg);
    lv_style_set_bg_color(&style_tab_bg, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&style_tab_bg, 200);

    styles_inited = true;
}

lv_obj_t * ui_Screen_Settings = NULL;
lv_obj_t * ui_Settings_Tabview_ConfigPanel = NULL;
lv_obj_t * ui_Settings_Button_closeConfig = NULL;
lv_obj_t * ui_Settings_Label_closeBtnText = NULL;
lv_obj_t * ui_Settings_Keyboard_Keyboard1 = NULL;
lv_obj_t * ui_Settings_Keyboard_Number = NULL;

void settings_activity_kick(lv_event_t *e)
{
    (void)e;
    ui_helpers_notify_activity();
}

void ui_event_Settings_Tabview_ConfigPanel(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_PRESSED || event_code == LV_EVENT_VALUE_CHANGED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Button_closeConfig(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        saveConfig(e);
        if (ui_Screen_Overview != NULL) {
            lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
        }
    }
}

void ui_event_Settings_Keyboard_Keyboard1(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t* keyboard = lv_event_get_target(e);

    if(event_code == LV_EVENT_READY) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(event_code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_event_Settings_Keyboard_Number(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t* keyboard = lv_event_get_target(e);

    if(event_code == LV_EVENT_READY) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(event_code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_Screen_Settings_screen_init(void)
{
    init_styles();

    ui_Screen_Settings = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen_Settings,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_font(ui_Screen_Settings, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_Screen_Settings, LV_OBJ_FLAG_GESTURE_BUBBLE);
    ESP_LOGI(TAG, "Added GESTURE_BUBBLE flag for Settings screen");

    ui_Settings_Tabview_ConfigPanel = lv_tabview_create(ui_Screen_Settings, LV_DIR_BOTTOM, 40);
    lv_obj_set_width(ui_Settings_Tabview_ConfigPanel, lv_pct(100));
    lv_obj_set_height(ui_Settings_Tabview_ConfigPanel, lv_pct(100));
    lv_obj_set_align(ui_Settings_Tabview_ConfigPanel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Settings_Tabview_ConfigPanel,
                      LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                      LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_style(ui_Settings_Tabview_ConfigPanel, &style_tab_bg, 0);
    lv_obj_set_style_text_color(ui_Settings_Tabview_ConfigPanel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Tabview_ConfigPanel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0x00ADFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 255,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0x025074),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), LV_GRAD_DIR_VER,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0x000000),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 255,
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 255,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0x9D9C9C),
                                   LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), LV_GRAD_DIR_VER,
                                 LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0x000000),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 255,
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 2,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_Settings_Tabview_ConfigPanel), 255,
                              LV_PART_ITEMS | LV_STATE_CHECKED);

    ui_Screen_Settings_WifiTab_init(ui_Settings_Tabview_ConfigPanel);

    ui_Screen_Settings_NasTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_ScreenTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_StationTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_MusicTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_RegionTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_GuideTab_init(ui_Settings_Tabview_ConfigPanel);
    ui_Screen_Settings_FanTab_init(ui_Settings_Tabview_ConfigPanel);

    ui_Settings_Button_closeConfig = lv_btn_create(ui_Screen_Settings);
    lv_obj_set_width(ui_Settings_Button_closeConfig, 50);
    lv_obj_set_height(ui_Settings_Button_closeConfig, 50);
    lv_obj_set_align(ui_Settings_Button_closeConfig, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_Settings_Button_closeConfig, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_Settings_Button_closeConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Settings_Button_closeConfig, &style_btn_border, 0);

    ui_Settings_Label_closeBtnText = lv_label_create(ui_Settings_Button_closeConfig);
    lv_obj_set_width(ui_Settings_Label_closeBtnText, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_closeBtnText, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_Settings_Label_closeBtnText, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Settings_Label_closeBtnText, "X");
    lv_obj_set_style_text_font(ui_Settings_Label_closeBtnText, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Keyboard_Keyboard1 = lv_keyboard_create(ui_Screen_Settings);
    lv_obj_set_width(ui_Settings_Keyboard_Keyboard1, 350);
    lv_obj_set_height(ui_Settings_Keyboard_Keyboard1, lv_pct(100));
    lv_obj_set_align(ui_Settings_Keyboard_Keyboard1, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_Settings_Keyboard_Keyboard1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(ui_Settings_Keyboard_Keyboard1, lv_color_hex(0x117200), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Keyboard_Keyboard1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_Settings_Keyboard_Keyboard1, lv_color_hex(0x143200), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_Settings_Keyboard_Keyboard1, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Keyboard_Number = lv_keyboard_create(ui_Screen_Settings);
    lv_keyboard_set_mode(ui_Settings_Keyboard_Number, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_width(ui_Settings_Keyboard_Number, 300);
    lv_obj_set_height(ui_Settings_Keyboard_Number, 172);
    lv_obj_set_align(ui_Settings_Keyboard_Number, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_flag(ui_Settings_Keyboard_Number, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ui_Settings_Keyboard_Number, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Settings_Keyboard_Number, lv_color_hex(0x0440FC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Keyboard_Number, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_Settings_Keyboard_Number, lv_color_hex(0x05037F), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_Settings_Keyboard_Number, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_keyboard_set_textarea(ui_Settings_Keyboard_Keyboard1, ui_Settings_Textarea_Password);
    lv_obj_add_event_cb(ui_Settings_Keyboard_Keyboard1, ui_event_Settings_Keyboard_Keyboard1, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(ui_Settings_Keyboard_Number, ui_Settings_Textarea_Latitude);
    lv_obj_add_event_cb(ui_Settings_Keyboard_Number, ui_event_Settings_Keyboard_Number, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Button_closeConfig, ui_event_Settings_Button_closeConfig, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabview_ConfigPanel, ui_event_Settings_Tabview_ConfigPanel, LV_EVENT_ALL, NULL);

    lv_obj_add_flag(ui_Settings_Tabview_ConfigPanel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(ui_Screen_Settings, ui_event_Screen_Settings_gesture, LV_EVENT_GESTURE, NULL);

}

void ui_Screen_Settings_screen_destroy(void)
{
    if(ui_Screen_Settings) lv_obj_del(ui_Screen_Settings);

    ui_Screen_Settings = NULL;
    ui_Settings_Tabview_ConfigPanel = NULL;
    ui_Settings_Button_closeConfig = NULL;
    ui_Settings_Label_closeBtnText = NULL;
    ui_Settings_Keyboard_Keyboard1 = NULL;
    ui_Settings_Keyboard_Number = NULL;

    ui_Screen_Settings_WifiTab_cleanup();
    ui_Screen_Settings_NasTab_cleanup();
    ui_Screen_Settings_ScreenTab_cleanup();
    ui_Screen_Settings_StationTab_cleanup();
    ui_Screen_Settings_MusicTab_cleanup();
    ui_Screen_Settings_RegionTab_cleanup();
    ui_Screen_Settings_GuideTab_cleanup();
    ui_Screen_Settings_FanTab_cleanup();
}