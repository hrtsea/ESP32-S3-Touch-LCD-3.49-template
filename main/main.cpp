#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
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

#include "recorder.h"
#include "webui.h"
#include "cli.h"
#include "i18n.h"
#include "landmask.h"

#include "app_cfg.h"
#include "disp_driver.h"
#include "esp_wifi_config.h"
#include "esp_bus.h"
#include "wifi_bridge.h"
#include "esp_http_server.h"
#include "sntp_manager.h"
#include "hw_init.h"
#include "ui.h"
#include "nas_event_loop.h"
#include "http_timer.h"
#include "event_bus.h"

static const char *TAG = "skeleton";

extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

static void log_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);
}

static void network_init(void)
{
    esp_bus_init();

    wifi_cfg_config_t cfg = {
        .default_networks = (wifi_network_t[]){
            { DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, 10 },
        },
        .default_network_count = (DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0]) ? 1 : 0,
        .max_retry_per_network = 3,
        .retry_interval_ms = 5000,
        .retry_max_interval_ms = 30000,
        .max_reconnect_attempts = 5,
        .on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,
        .provisioning_mode = WIFI_PROV_ON_FAILURE,
        .stop_provisioning_on_connect = true,
        .http_post_prov_mode = WIFI_HTTP_API_ONLY,
        .default_ap = {
            .ssid = DEFAULT_AP_SSID,
            .password = DEFAULT_AP_PASSWORD,
        },
        .enable_ap = true,
        .http = {
            .api_base_path = "/api/wifi",
            .enable_auth = true,
            .auth_username = WEBUI_AUTH_USER,
            .auth_password = WEBUI_AUTH_PASSWORD,
        },
    };
    wifi_cfg_init(&cfg);

    wifi_bridge_init();

    webui_set_auth(WEBUI_AUTH_USER, WEBUI_AUTH_PASSWORD);

    httpd_handle_t srv = wifi_cfg_get_httpd();
    if (srv) {
        if (webui_start_with_httpd(srv) != ESP_OK) {
            ESP_LOGW(TAG, "webui_start_with_httpd failed");
        }
    } else {
        if (webui_start() != ESP_OK) {
            ESP_LOGW(TAG, "webui_start failed");
        }
    }
}

extern "C" void app_main(void)
{
    log_init();
    
    event_bus_init();
    
    app_cfg_load();
    
    ESP_LOGI(TAG, "===== ZotLab NAS Monitor boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    hw_init();

    network_init();

    nas_event_loop_start();

    http_timer_init();
    http_timer_start();

    ui_init();

    cli_start();

    ESP_LOGI(TAG, "===== boot complete, entering main loop =====");
    system_monitor_start();
}


