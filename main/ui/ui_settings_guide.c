#include "ui_settings_guide.h"
#include "ui_settings_internal.h"
#include "ui_events.h"
#include "ui_helpers.h"

LV_FONT_DECLARE(lv_font_montserrat_18);
LV_IMG_DECLARE(ui_img_images_button_png);

lv_obj_t * ui_Settings_Tabpage_guide = NULL;
lv_obj_t * ui_Settings_Textarea_UserGuide = NULL;
lv_obj_t * ui_Settings_Image_buttons = NULL;

static void ui_event_Settings_Tabpage_guide(lv_event_t * e);
static void ui_event_Settings_Textarea_UserGuide(lv_event_t * e);

void ui_event_Settings_Tabpage_guide(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_event_Settings_Textarea_UserGuide(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if(event_code == LV_EVENT_PRESSED) {
        settings_activity_kick(e);
    }
}

void ui_Screen_Settings_GuideTab_init(lv_obj_t *parent)
{
    ui_Settings_Tabpage_guide = lv_tabview_add_tab(parent, "User Guide");
    lv_obj_set_scrollbar_mode(ui_Settings_Tabpage_guide, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_Settings_Tabpage_guide, LV_DIR_VER);

    ui_Settings_Textarea_UserGuide = lv_textarea_create(ui_Settings_Tabpage_guide);
    lv_obj_set_width(ui_Settings_Textarea_UserGuide, 640);
    lv_obj_set_height(ui_Settings_Textarea_UserGuide, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Textarea_UserGuide, LV_ALIGN_TOP_MID, 0, -17);
    lv_textarea_set_text(ui_Settings_Textarea_UserGuide,
                         "[Wi-Fi ] - Automatic Connection Setup\n\nSet up your device to automatically connect to your preferred Wi-Fi networks (up to 10 SSIDs).\n- Tap SCAN to search for nearby networks.\n- Select the network you wish to connect to from the dropdown list.\n- Enter the network password.\n- Tap SAVE to save the password\n- Please wait a moment, device is automatically connect to the network.\n\n[ Screen ] - Display Settings\n\nAdjust the behavior of your device's screen.\n- Set Screen Brightness: Adjust the display intensity.\n- Set Screen Off Timer: Control how long the screen stays on after inactivity.\n- Setting the timer to 0 means the screen will always stay on.\n\n[ Radio ] - Custom Radio Station List\n\nYou can create and upload a custom list of radio stations.\n- Create your own stations list using the CSV format in a file named stations.csv (title,url http only)\n- Save the stations.csv file onto your SD Card.\n- Tap UPLOAD to load your custom station list.\n- If the stations.csv file is not available, the device will use the default station list.\n\n[ Music ] - Update Music Library\n\nLoad audio files from your SD Card to create your music library.\n- Supported audio formats include: mp3, flac, aac, and ogg.\n- Copy your audio files to the SD Card. Files can be placed in the root directory or in subfolders (up to 5 levels deep).\n- Tap LOAD to build the music library.\n* Note: This process may take some time, depending on the number of audio files on the SD Card.\n\n[ Region ] - Set weather & clock region\n\n* Auto by IP - the location will be automatically detected by IP\n* Manual -  enter specific Latitude / Longitude\n* UTC offset - set timezone offset\n* Temp - set weather unit Celcius / Farenheit\n\n[ Button Control ]\n\n           [ RESET    ] -  Reset TuneBar\n\n           [ POWER ] - Hold a sec to turn on/ hold 3 sec to turn off\n\n           [  LIGHT  ] - Toggle screen ON/OFF");
    lv_obj_add_flag(ui_Settings_Textarea_UserGuide, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(ui_Settings_Textarea_UserGuide, LV_DIR_VER);
    lv_obj_set_style_text_color(ui_Settings_Textarea_UserGuide, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Settings_Textarea_UserGuide, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Settings_Textarea_UserGuide, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Settings_Textarea_UserGuide, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings_Image_buttons = lv_img_create(ui_Settings_Textarea_UserGuide);
    lv_img_set_src(ui_Settings_Image_buttons, &ui_img_images_button_png);
    lv_obj_set_width(ui_Settings_Image_buttons, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_Settings_Image_buttons, LV_SIZE_CONTENT);
    lv_obj_align(ui_Settings_Image_buttons, LV_ALIGN_BOTTOM_LEFT, 0, 10);
    lv_obj_add_flag(ui_Settings_Image_buttons, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Settings_Image_buttons, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(ui_Settings_Textarea_UserGuide, ui_event_Settings_Textarea_UserGuide, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Settings_Tabpage_guide, ui_event_Settings_Tabpage_guide, LV_EVENT_ALL, NULL);
}

void ui_Screen_Settings_GuideTab_cleanup(void)
{
    ui_Settings_Tabpage_guide = NULL;
    ui_Settings_Textarea_UserGuide = NULL;
    ui_Settings_Image_buttons = NULL;
}