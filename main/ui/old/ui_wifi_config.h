#ifndef UI_WIFI_CONFIG_H
#define UI_WIFI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *wifi_list;
    lv_obj_t *kb_overlay;
    lv_obj_t *kb_textarea;
} wifi_config_ui_t;

void wifi_config_create(lv_obj_t *parent);
void wifi_config_cleanup(void);
void wifi_config_refresh_status(void);
void wifi_config_refresh_list(void);

#ifdef __cplusplus
}
#endif

#endif
