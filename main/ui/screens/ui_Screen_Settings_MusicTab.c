#include "../ui.h"
#include "ui_Screen_Settings_MusicTab.h"

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_32);

lv_obj_t * ui_Settings_Tabpage_music = NULL;
lv_obj_t * ui_Settings_Button_scanMusic = NULL;
lv_obj_t * ui_Settings_Label_scanMusicBtnText = NULL;
lv_obj_t * ui_Settings_Label_musicHint = NULL;
lv_obj_t * ui_Settings_Label_trackCount = NULL;

static void ui_event_Settings_Tabpage_music(lv_event_t * e);
static void ui_event_Settings_Button_scanMusic(lv_event_t * e);

void ui_event_Settings_Tabpage_music(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Button_scanMusic(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_CLICKED) {
        loadMusicFromSDCARD(e);
    }
}

void ui_Screen_Settings_MusicTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_music = lv_tabview_add_tab(parent, "Music");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_music, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_music, LV_DIR_HOR);

    ui_Settings_Button_scanMusic = lv_btn_create(ui_Settings_Tabpage_music);
    lv_obj_set_width(ui_Settings_Button_scanMusic, 144);
    lv_obj_set_height(ui_Settings_Button_scanMusic, 50);
    lv_obj_align(ui_Settings_Button_scanMusic, LV_ALIGN_BOTTOM_LEFT, -6, 0);
    lv_obj_add_flag(ui_Settings_Button_scanMusic, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_Settings_Button_scanMusic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(ui_Settings_Button_scanMusic, &style_btn_border, 0);

    ui_Settings_Label_scanMusicBtnText = lv_label_create(ui_Settings_Button_scanMusic);
    lv_obj_set_width(ui_Settings_Label_scanMusicBtnText, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_scanMusicBtnText, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_scanMusicBtnText, LV_ALIGN_CENTER, 1, 0);
    lv_label_set_text(ui_Settings_Label_scanMusicBtnText, "D Load");
    lv_obj_set_style_text_font(ui_Settings_Label_scanMusicBtnText, &lv_font_montserrat_32, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Label_musicHint = lv_label_create(ui_Settings_Tabpage_music);
    lv_obj_set_width(ui_Settings_Label_musicHint, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_musicHint, LV_SIZE_CONTENT);
    lv_label_set_text(ui_Settings_Label_musicHint, "Tap  LOAD to refresh the music library from the SD card");

    ui_Settings_Label_trackCount = lv_label_create(ui_Settings_Tabpage_music);
    lv_obj_set_width(ui_Settings_Label_trackCount, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Label_trackCount, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Label_trackCount, LV_ALIGN_BOTTOM_LEFT, 150, -9);
    lv_label_set_long_mode(ui_Settings_Label_trackCount, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ui_Settings_Label_trackCount, "Card Mount Failed");
    lv_obj_set_style_text_font(ui_Settings_Label_trackCount, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Settings_Button_scanMusic, ui_event_Settings_Button_scanMusic, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_music, ui_event_Settings_Tabpage_music, LV_EVENT_ALL, NULL);
}

void ui_Screen_Settings_MusicTab_cleanup(void)
{
    ui_Settings_Tabpage_music = NULL;
    ui_Settings_Button_scanMusic = NULL;
    ui_Settings_Label_scanMusicBtnText = NULL;
    ui_Settings_Label_musicHint = NULL;
    ui_Settings_Label_trackCount = NULL;
}