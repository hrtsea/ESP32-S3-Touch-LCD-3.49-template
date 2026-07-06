#include "ui_Screen_Settings_RegionTab.h"
#include "ui_Screen_Settings.h"
#include "ui_events.h"
#include "ui_helpers.h"

LV_FONT_DECLARE(lv_font_montserrat_16);
LV_IMG_DECLARE(ui_img_images_globe_png);
LV_IMG_DECLARE(ui_img_images_timezone_png);

lv_obj_t * ui_Settings_Tabpage_region = NULL;
lv_obj_t * ui_Settings_Image_Globe = NULL;
lv_obj_t * ui_Settings_Checkbox_AutoIP = NULL;
lv_obj_t * ui_Settings_Textarea_Latitude = NULL;
lv_obj_t * ui_Settings_Textarea_Longitude = NULL;
lv_obj_t * ui_Settings_Image_timezone = NULL;
lv_obj_t * ui_Settings_Roller_Hour = NULL;
lv_obj_t * ui_Settings_Roller_Minute = NULL;
lv_obj_t * ui_Settings_Roller_Unit = NULL;
lv_obj_t * ui_Settings_Label_timezoneHint = NULL;

static void ui_event_Settings_Tabpage_region(lv_event_t * e);
static void ui_event_Settings_Checkbox_AutoIP(lv_event_t * e);
static void ui_event_Settings_Textarea_Latitude(lv_event_t * e);
static void ui_event_Settings_Textarea_Longitude(lv_event_t * e);
static void ui_event_Settings_Roller_Hour(lv_event_t * e);
static void ui_event_Settings_Roller_Minute(lv_event_t * e);
static void ui_event_Settings_Roller_Unit(lv_event_t * e);

void ui_event_Settings_Tabpage_region(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Checkbox_AutoIP(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    if(event_code == LV_EVENT_VALUE_CHANGED && lv_obj_has_state(target, LV_STATE_CHECKED)) {
        set_query_para_autoip(e);
        _ui_flag_modify(ui_Settings_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
        _ui_flag_modify(ui_Settings_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    }
    if(event_code == LV_EVENT_VALUE_CHANGED && !lv_obj_has_state(target, LV_STATE_CHECKED)) {
        _ui_flag_modify(ui_Settings_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
        _ui_flag_modify(ui_Settings_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_Settings_Textarea_Latitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_Settings_Keyboard_Number, ui_Settings_Textarea_Latitude);
        _ui_flag_modify(ui_Settings_Keyboard_Number, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_Settings_Textarea_Longitude(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        _ui_keyboard_set_target(ui_Settings_Keyboard_Number, ui_Settings_Textarea_Longitude);
        _ui_flag_modify(ui_Settings_Keyboard_Number, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
}

void ui_event_Settings_Roller_Hour(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setOffsetHour(e);
    }
}

void ui_event_Settings_Roller_Minute(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setOffsetMinute(e);
    }
}

void ui_event_Settings_Roller_Unit(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_VALUE_CHANGED) {
        setTempUnit(e);
    }
}

void ui_Screen_Settings_RegionTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_region = lv_tabview_add_tab(parent, "Region");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_region, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_region, LV_DIR_HOR);

    ui_Settings_Image_Globe = lv_img_create(ui_Settings_Tabpage_region);
    lv_img_set_src(ui_Settings_Image_Globe, &ui_img_images_globe_png);
    lv_obj_set_width(ui_Settings_Image_Globe, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Image_Globe, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Image_Globe, LV_ALIGN_LEFT_MID, -12, 0);
    lv_obj_add_flag(ui_Settings_Image_Globe, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Settings_Image_Globe, LV_OBJ_FLAG_SCROLLABLE);

    ui_Settings_Checkbox_AutoIP = lv_checkbox_create(ui_Settings_Tabpage_region);
    lv_checkbox_set_text(ui_Settings_Checkbox_AutoIP, "Auto by IP");
    lv_obj_set_width(ui_Settings_Checkbox_AutoIP, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Checkbox_AutoIP, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Checkbox_AutoIP, LV_ALIGN_CENTER, 118, -11);
    lv_obj_add_flag(ui_Settings_Checkbox_AutoIP, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    ui_Settings_Textarea_Latitude = lv_textarea_create(ui_Settings_Tabpage_region);
    lv_obj_set_width(ui_Settings_Textarea_Latitude, 150);
    lv_obj_set_height(ui_Settings_Textarea_Latitude, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Textarea_Latitude, LV_ALIGN_CENTER, 117, 23);
    lv_textarea_set_accepted_chars(ui_Settings_Textarea_Latitude, "0123456789.-");
    lv_textarea_set_max_length(ui_Settings_Textarea_Latitude, 12);
    lv_textarea_set_placeholder_text(ui_Settings_Textarea_Latitude, "Latitude");
    lv_textarea_set_one_line(ui_Settings_Textarea_Latitude, true);
    lv_obj_set_scrollbar_mode(ui_Settings_Textarea_Latitude, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(ui_Settings_Textarea_Latitude, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_Latitude, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Settings_Textarea_Latitude, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Settings_Textarea_Latitude, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_border_color(ui_Settings_Textarea_Latitude, lv_color_hex(0x000000), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_Settings_Textarea_Latitude, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_Settings_Textarea_Latitude, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_Settings_Textarea_Latitude, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_set_style_text_color(ui_Settings_Textarea_Latitude, lv_color_hex(0x555555),
                                LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_Latitude, 255, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);

    ui_Settings_Textarea_Longitude = lv_textarea_create(ui_Settings_Tabpage_region);
    lv_obj_set_width(ui_Settings_Textarea_Longitude, 150);
    lv_obj_set_height(ui_Settings_Textarea_Longitude, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Textarea_Longitude, LV_ALIGN_CENTER, 117, 70);
    lv_textarea_set_accepted_chars(ui_Settings_Textarea_Longitude, "0123456789.-");
    lv_textarea_set_max_length(ui_Settings_Textarea_Longitude, 12);
    lv_textarea_set_placeholder_text(ui_Settings_Textarea_Longitude, "Longitude");
    lv_textarea_set_one_line(ui_Settings_Textarea_Longitude, true);
    lv_obj_set_scrollbar_mode(ui_Settings_Textarea_Longitude, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(ui_Settings_Textarea_Longitude, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_Longitude, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Settings_Textarea_Longitude, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Settings_Textarea_Longitude, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_border_color(ui_Settings_Textarea_Longitude, lv_color_hex(0x000000),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ui_Settings_Textarea_Longitude, 255, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ui_Settings_Textarea_Longitude, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ui_Settings_Textarea_Longitude, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);

    lv_obj_set_style_text_color(ui_Settings_Textarea_Longitude, lv_color_hex(0x555555),
                                LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_Longitude, 255, LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);

    ui_Settings_Image_timezone = lv_img_create(ui_Settings_Tabpage_region);
    lv_img_set_src(ui_Settings_Image_timezone, &ui_img_images_timezone_png);
    lv_obj_set_width(ui_Settings_Image_timezone, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Image_timezone, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Image_timezone, LV_ALIGN_CENTER, 24, 0);
    lv_obj_add_flag(ui_Settings_Image_timezone, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Settings_Image_timezone, LV_OBJ_FLAG_SCROLLABLE);

    ui_Settings_Roller_Hour = lv_roller_create(ui_Settings_Tabpage_region);
    lv_roller_set_options(ui_Settings_Roller_Hour,
                          "+14\n+13\n+12\n+11\n+10\n+9\n+8\n+7\n+6\n+5\n+4\n+3\n+2\n+1\n0\n-1\n-2\n-3\n-4\n-5\n-6\n-7\n-8\n-9\n-10\n-11\n-12",
                          LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_Settings_Roller_Hour, 14, LV_ANIM_OFF);
    lv_obj_set_width(ui_Settings_Roller_Hour, 50);
    lv_obj_set_height(ui_Settings_Roller_Hour, 100);
    lv_obj_align(ui_Settings_Roller_Hour, LV_ALIGN_RIGHT_MID, -180, 10);

    ui_Settings_Roller_Minute = lv_roller_create(ui_Settings_Tabpage_region);
    lv_roller_set_options(ui_Settings_Roller_Minute, "+45\n+30\n+15\n0\n-15\n-30\n-45", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_Settings_Roller_Minute, 3, LV_ANIM_OFF);
    lv_obj_set_width(ui_Settings_Roller_Minute, 50);
    lv_obj_set_height(ui_Settings_Roller_Minute, 100);
    lv_obj_align(ui_Settings_Roller_Minute, LV_ALIGN_RIGHT_MID, -110, 10);

    ui_Settings_Roller_Unit = lv_roller_create(ui_Settings_Tabpage_region);
    lv_roller_set_options(ui_Settings_Roller_Unit, "°C\n°F", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ui_Settings_Roller_Unit, 0, LV_ANIM_OFF);
    lv_obj_set_width(ui_Settings_Roller_Unit, 50);
    lv_obj_set_height(ui_Settings_Roller_Unit, 100);
    lv_obj_align(ui_Settings_Roller_Unit, LV_ALIGN_RIGHT_MID, -40, 10);

    ui_Settings_Label_timezoneHint = lv_label_create(ui_Settings_Tabpage_region);
    lv_obj_set_width(ui_Settings_Label_timezoneHint, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_timezoneHint, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_timezoneHint, LV_ALIGN_TOP_RIGHT, -49, -17);
    lv_label_set_text(ui_Settings_Label_timezoneHint, "UTC Offset :Hour   Minute   Unit");
    lv_obj_set_style_text_align(ui_Settings_Label_timezoneHint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Settings_Checkbox_AutoIP, ui_event_Settings_Checkbox_AutoIP, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Textarea_Latitude, ui_event_Settings_Textarea_Latitude, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Textarea_Longitude, ui_event_Settings_Textarea_Longitude, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Roller_Hour, ui_event_Settings_Roller_Hour, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Roller_Minute, ui_event_Settings_Roller_Minute, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Roller_Unit, ui_event_Settings_Roller_Unit, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_region, ui_event_Settings_Tabpage_region, LV_EVENT_ALL, NULL);
}

void ui_Screen_Settings_RegionTab_cleanup(void)
{
    ui_Settings_Tabpage_region = NULL;
    ui_Settings_Image_Globe = NULL;
    ui_Settings_Checkbox_AutoIP = NULL;
    ui_Settings_Textarea_Latitude = NULL;
    ui_Settings_Textarea_Longitude = NULL;
    ui_Settings_Image_timezone = NULL;
    ui_Settings_Roller_Hour = NULL;
    ui_Settings_Roller_Minute = NULL;
    ui_Settings_Roller_Unit = NULL;
    ui_Settings_Label_timezoneHint = NULL;
}