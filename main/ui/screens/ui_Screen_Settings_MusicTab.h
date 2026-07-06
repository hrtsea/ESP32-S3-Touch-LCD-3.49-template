#ifndef UI_SCREEN_SETTINGS_MUSICTAB_H
#define UI_SCREEN_SETTINGS_MUSICTAB_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t * ui_Settings_Tabpage_music;
extern lv_obj_t * ui_Settings_Button_scanMusic;
extern lv_obj_t * ui_Settings_Label_scanMusicBtnText;
extern lv_obj_t * ui_Settings_Label_musicHint;
extern lv_obj_t * ui_Settings_Label_trackCount;

void ui_Screen_Settings_MusicTab_init(lv_obj_t *parent);
void ui_Screen_Settings_MusicTab_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_SETTINGS_MUSICTAB_H */