#include "ui_settings_station.h"
#include "ui_settings_internal.h"
#include "ui_events.h"
#include "ui_helpers.h"

LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(ui_font_NotoSanThai16);

lv_obj_t * ui_Settings_Tabpage_station = NULL;
lv_obj_t * ui_Settings_Textarea_stationList = NULL;
lv_obj_t * ui_Settings_Label_stationHint = NULL;
lv_obj_t * ui_Settings_Button_LoadStation = NULL;
lv_obj_t * ui_Settings_Label_loadStationBtnText = NULL;

static void ui_event_Settings_Tabpage_station(lv_event_t * e);
static void ui_event_Settings_Button_LoadStation(lv_event_t * e);

void ui_event_Settings_Tabpage_station(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Button_LoadStation(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        loadStationFromSDCARD(e);
    }
}

void ui_Screen_Settings_StationTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_station = lv_tabview_add_tab(parent, "Radio");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_station, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_station, LV_DIR_HOR);

    ui_Settings_Textarea_stationList = lv_textarea_create(ui_Settings_Tabpage_station);
    lv_obj_set_width(ui_Settings_Textarea_stationList, 320);
    lv_obj_set_height(ui_Settings_Textarea_stationList, 128);
    lv_obj_align(ui_Settings_Textarea_stationList, LV_ALIGN_CENTER, -12, -16);
    lv_textarea_set_placeholder_text(ui_Settings_Textarea_stationList, "Station URL List");
    lv_obj_set_scroll_dir(ui_Settings_Textarea_stationList, LV_DIR_VER);
    lv_obj_set_style_text_color(ui_Settings_Textarea_stationList, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_stationList, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Settings_Textarea_stationList, &ui_font_NotoSanThai16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Settings_Textarea_stationList, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Textarea_stationList, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Label_stationHint = lv_label_create(ui_Settings_Tabpage_station);
    lv_obj_set_width(ui_Settings_Label_stationHint, 300);
    lv_obj_set_height(ui_Settings_Label_stationHint, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_stationHint, LV_ALIGN_BOTTOM_RIGHT, 11, 13);
    lv_label_set_text(ui_Settings_Label_stationHint,
                      "Insert a microSD card with filename\n[ stations.csv ]  click Load.\nDefaults will be used\nif the file is unavailable.");
    lv_obj_set_scrollbar_mode(ui_Settings_Label_stationHint, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(ui_Settings_Label_stationHint, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Button_LoadStation = lv_btn_create(ui_Settings_Tabpage_station);
    lv_obj_set_width(ui_Settings_Button_LoadStation, 144);
    lv_obj_set_height(ui_Settings_Button_LoadStation, 50);
    lv_obj_align(ui_Settings_Button_LoadStation, LV_ALIGN_TOP_MID, 88, -13);
    lv_obj_add_flag(ui_Settings_Button_LoadStation, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_Settings_Button_LoadStation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Settings_Button_LoadStation, &style_btn_border, 0);

    ui_Settings_Label_loadStationBtnText = lv_label_create(ui_Settings_Button_LoadStation);
    lv_obj_set_width(ui_Settings_Label_loadStationBtnText, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_loadStationBtnText, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_loadStationBtnText, LV_ALIGN_CENTER, 1, 0);
    lv_label_set_text(ui_Settings_Label_loadStationBtnText, "D Load");
    lv_obj_set_style_text_font(ui_Settings_Label_loadStationBtnText, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Settings_Button_LoadStation, ui_event_Settings_Button_LoadStation, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_station, ui_event_Settings_Tabpage_station, LV_EVENT_ALL, NULL);
}

void ui_Screen_Settings_StationTab_cleanup(void)
{
    ui_Settings_Tabpage_station = NULL;
    ui_Settings_Textarea_stationList = NULL;
    ui_Settings_Label_stationHint = NULL;
    ui_Settings_Button_LoadStation = NULL;
    ui_Settings_Label_loadStationBtnText = NULL;
}