#include "../ui.h"
#include "ui_Screen_Boot.h"
#include "app_info.h"
#include <math.h>
#include "esp_log.h"

lv_obj_t* ui_Screen_Boot = NULL;
static lv_obj_t* ui_LogoLabel = NULL;
static lv_obj_t* ui_VersionLabel = NULL;
static lv_obj_t* ui_ProgressBar = NULL;
static lv_obj_t* ui_StatusLabel = NULL;
static lv_timer_t* ui_TimeoutTimer = NULL;
static lv_timer_t* ui_ProgressTimer = NULL;
static lv_timer_t* ui_BreatheTimer = NULL;
static uint8_t ui_CurrentStage = 0;
static bool ui_TimerActive = false;

static void ui_Screen_Boot_timeout_callback(lv_timer_t* timer);
static void ui_Screen_Boot_breathe_callback(lv_timer_t* timer);

typedef struct {
    uint8_t stage;
    const char *message;
    uint8_t progress;
} boot_stage_t;

static const boot_stage_t s_boot_stages[] = {
    {1, "Loading system...",     25},
    {2, "Initializing WiFi...",  50},
    {3, "Ready",                 75},
    {0, NULL,                   100}
};

static void ui_Screen_Boot_progress_callback(lv_timer_t* timer)
{
    (void)timer;
    
    static uint8_t idx = 0;
    
    if (s_boot_stages[idx].message != NULL) {
        ui_CurrentStage = s_boot_stages[idx].stage;
        lv_bar_set_value(ui_ProgressBar, s_boot_stages[idx].progress, LV_ANIM_ON);
        lv_label_set_text(ui_StatusLabel, s_boot_stages[idx].message);
        idx++;
    } else {
        lv_bar_set_value(ui_ProgressBar, 100, LV_ANIM_ON);
        lv_label_set_text(ui_StatusLabel, "Complete");
        
        lv_timer_del(ui_ProgressTimer);
        ui_ProgressTimer = NULL;
        
        ui_TimeoutTimer = lv_timer_create(ui_Screen_Boot_timeout_callback, 500, NULL);
        ui_TimerActive = true;
    }
}

static void ui_Screen_Boot_breathe_callback(lv_timer_t* timer)
{
    (void)timer;
    
    static uint32_t s_tick = 0;
    s_tick++;
    
    float t = (float)s_tick * 0.05f;
    float alpha = (cosf(t) + 1.0f) * 0.5f;
    alpha = alpha * 0.4f + 0.6f;
    
    uint8_t opa = (uint8_t)(alpha * 255);
    lv_obj_set_style_text_opa(ui_LogoLabel, opa, 0);
    lv_obj_set_style_text_opa(ui_VersionLabel, opa, 0);
}

static void ui_Screen_Boot_timeout_callback(lv_timer_t* timer)
{
    (void)timer;
    ui_TimerActive = false;

    if (ui_Screen_Overview == NULL) {
        ui_Screen_Overview_screen_init();
    }
    lv_scr_load(ui_Screen_Overview);

    ui_Screen_Boot_stop_timeout();
}

void ui_Screen_Boot_screen_init(void)
{
    ui_Screen_Boot = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen_Boot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen_Boot, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(ui_Screen_Boot, 0, 0);

    lv_obj_add_event_cb(ui_Screen_Boot, ui_Screen_Boot_event_handler, LV_EVENT_ALL, NULL);

    ui_LogoLabel = lv_label_create(ui_Screen_Boot);
    static char logo_str[32];
    snprintf(logo_str, sizeof(logo_str), "%s %s", NAS_LOGO, NAS_TYPE);
    lv_label_set_text(ui_LogoLabel, logo_str);
    lv_obj_set_style_text_font(ui_LogoLabel, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ui_LogoLabel, lv_color_hex(0x00E676), 0);
    lv_obj_align(ui_LogoLabel, LV_ALIGN_CENTER, 0, -40);

    ui_VersionLabel = lv_label_create(ui_Screen_Boot);
    static char version_str[16];
    snprintf(version_str, sizeof(version_str), "v%s", APP_VERSION);
    lv_label_set_text(ui_VersionLabel, version_str);
    lv_obj_set_style_text_font(ui_VersionLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_VersionLabel, lv_color_hex(0x888888), 0);
    lv_obj_align(ui_VersionLabel, LV_ALIGN_CENTER, 0, 10);

    ui_ProgressBar = lv_bar_create(ui_Screen_Boot);
    lv_obj_set_size(ui_ProgressBar, 200, 8);
    lv_obj_align(ui_ProgressBar, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(ui_ProgressBar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(ui_ProgressBar, lv_color_hex(0x00E676), LV_PART_INDICATOR);

    ui_StatusLabel = lv_label_create(ui_Screen_Boot);
    lv_label_set_text(ui_StatusLabel, "Initializing system...");
    lv_obj_set_style_text_font(ui_StatusLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui_StatusLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_StatusLabel, LV_ALIGN_CENTER, 0, 60);

    ui_BreatheTimer = lv_timer_create(ui_Screen_Boot_breathe_callback, 30, NULL);

    ESP_LOGI("Boot", "Boot screen initialized");
}

void ui_Screen_Boot_start_progress(void)
{
    if (ui_ProgressTimer) return;
    
    ui_CurrentStage = 0;
    lv_bar_set_value(ui_ProgressBar, 0, LV_ANIM_OFF);
    lv_label_set_text(ui_StatusLabel, "Initializing system...");
    
    ui_ProgressTimer = lv_timer_create(ui_Screen_Boot_progress_callback, 800, NULL);
}

void ui_Screen_Boot_screen_cleanup(void)
{
    if (ui_Screen_Boot != NULL) {
        lv_obj_del(ui_Screen_Boot);
        ui_Screen_Boot = NULL;
    }
    ui_Screen_Boot_stop_timeout();
    if (ui_ProgressTimer) {
        lv_timer_del(ui_ProgressTimer);
        ui_ProgressTimer = NULL;
    }
    if (ui_BreatheTimer) {
        lv_timer_del(ui_BreatheTimer);
        ui_BreatheTimer = NULL;
    }
    ESP_LOGI("Boot", "Boot screen cleaned up");
}

void ui_Screen_Boot_update_progress(uint8_t stage, const char* message)
{
    if (ui_Screen_Boot == NULL) return;

    ui_CurrentStage = stage;

    uint8_t progress = (stage * 25);
    lv_bar_set_value(ui_ProgressBar, progress, LV_ANIM_ON);

    if (message != NULL) {
        lv_label_set_text(ui_StatusLabel, message);
    }
}

void ui_Screen_Boot_stop_timeout(void)
{
    if (ui_TimeoutTimer != NULL) {
        lv_timer_del(ui_TimeoutTimer);
        ui_TimeoutTimer = NULL;
        ui_TimerActive = false;
    }
}

bool ui_Screen_Boot_is_active(void)
{
    return (ui_Screen_Boot != NULL);
}