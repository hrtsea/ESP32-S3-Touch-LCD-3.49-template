#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"

#include "esp_io_expander_tca9554.h"
#include "driver/i2c_master.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "audio_min.h"
#include "radio.h"
#include "recorder.h"
#include "webui.h"
#include "cli.h"
#include "i18n.h"
#include "landmask.h"
#include "tz_cities.h"

#include "app_cfg.h"
#include "disp_driver.h"
#include "wifi_manager.h"
#include "sntp_manager.h"
#include "bg_fetcher.h"
#include "hw_init.h"
#include "system_monitor.h"
#include "ui_state.h"
#include "ui_main.h"
#include "ui_radio.h"
#include "ui_clock.h"
#include "ui_quotes.h"

static const char *TAG = "skeleton";

extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

extern "C" void tz_apply_current(void);
extern "C" const char *tz_current_city_name(void);

extern void wifi_connect(const char *ssid, const char *pass);

static void on_backlight_changed(uint8_t brightness)
{
    ui_state_set_dim_state(0);
    ui_state_set_last_activity_ms(lv_tick_get());
    backlight_apply(brightness);
}

static void on_bg_fetch_ensure(void)
{
    bg_fetcher_ensure();
}

static void on_wifi_connect(const char *ssid, const char *pass)
{
    wifi_connect(ssid, pass);
}

static void log_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);
}

static void network_init(void)
{
    wifi_manager_init();

    if (webui_start() != ESP_OK) {
        ESP_LOGW(TAG, "webui_start failed");
    }
}

static void ui_init(void)
{
    char buf[256];
    ui_state_get_status_text(buf, sizeof(buf));
    show_main_ui(buf);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
}

static void cli_init(void)
{
    cli_start();
}

extern "C" void app_main(void)
{
    log_init();
    
    app_cfg_load();
    
    ESP_LOGI(TAG, "===== 12_HelloWorld_Skeleton boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    hw_init();

    tz_apply_current();
    network_init();
    ui_init();

    app_cfg_callbacks_t cfg_cbs = {
        .on_backlight_changed = on_backlight_changed,
        .on_bg_fetch_ensure = on_bg_fetch_ensure,
        .on_wifi_connect = on_wifi_connect,
    };
    app_cfg_register_callbacks(&cfg_cbs);

    cli_init();

    radio_engine_warm_at_boot();
    bg_fetcher_ensure();

    system_monitor_start();
}