#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "i18n.h"
#include "tz_cities.h"
#include "landmask.h"

#include "app_cfg.h"
#include "config.h"
#include "disp_driver.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "theme.h"
#include "event_bus.h"
#include "nas_data.h"
#include "data_source.h"
#include "ui_helpers.h"
#include "ui_events.h"

#include "screens/ui_Screen_Boot.h"
#include "screens/ui_Screen_Overview.h"
#include "screens/ui_Screen_Settings.h"
#include "screens/ui_Screen_Storage.h"
#include "screens/ui_Screen_WifiConfig.h"

#define N_TILES 4

#define menu_input_blocked() ui_helpers_menu_input_blocked()

void ui_init(void);


#ifdef __cplusplus
}
#endif

#endif /* UI_H */
