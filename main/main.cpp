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
#include "ui_common.h"
#include "ui_radio.h"

static const char *TAG = "skeleton";

extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

extern "C" void tz_apply_current(void);
extern "C" const char *tz_current_city_name(void);
extern "C" void disp_driver_init(void);

extern "C" int webui_snapshot_fb(void *out, size_t cap)
{
    if (!out) return -1;
    size_t need = (size_t)UI_CANVAS_W * UI_CANVAS_H * 2;
    if (cap < need) return -1;
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || !disp->driver || !disp->driver->draw_buf) return -1;
    const void *src = disp->driver->draw_buf->buf1;
    if (!src) return -1;
    if (!lvgl_lock(50)) return -1;
    memcpy(out, src, need);
    lvgl_unlock();
    return (int)need;
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);

    {
        esp_err_t er = nvs_flash_init();
        if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            er = nvs_flash_init();
        }
        ESP_ERROR_CHECK(er);
    }
    cfg_load();
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             tz_current_city_name(), k_tz_cities[g_cfg.tz_idx].posix_tz,
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");
    ESP_LOGI(TAG, "===== 12_HelloWorld_Skeleton boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    int pos = 0;
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Drivers:\n");

    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "I2C OK\n");

    ESP_LOGI(TAG, "[2/9] TCA9554 power rails P6+P7=HIGH");
    {
        esp_io_expander_handle_t io_expander = NULL;
        esp_err_t er = esp_io_expander_new_i2c_tca9554(
            esp_i2c_bus_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
        ESP_LOGI(TAG, "      tca9554 new=%s handle=%p", esp_err_to_name(er), io_expander);
        ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT));
        ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander,
            IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_6, 1));
        esp_io_expander_print_state(io_expander);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(0xFF - g_cfg.brightness));
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel + LVGL");
    disp_driver_init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "LCD/Touch OK\n");

    ESP_LOGI(TAG, "[5/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "RTC/IMU OK\n");

    ESP_LOGI(TAG, "[6/9] ADC battery");
    adc_bsp_init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "ADC OK\n");

    ESP_LOGI(TAG, "[7/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Audio OK\n");
    } else {
        pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "Audio FAIL\n");
    }

    ESP_LOGI(TAG, "[8/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    pos += snprintf(g_status_text + pos, sizeof(g_status_text) - pos, "SD/Btn OK");

    setenv("TZ", "UTC0", 1);
    tzset();
    {
        time_t build_epoch = (time_t)BUILD_EPOCH_UTC;
        struct tm bt = {};
        gmtime_r(&build_epoch, &bt);
        int b_y = bt.tm_year + 1900;
        int b_mo = bt.tm_mon + 1;
        int b_d = bt.tm_mday;
        int b_h = bt.tm_hour;
        int b_mi = bt.tm_min;
        int b_s = bt.tm_sec;

        RtcDateTime_t r = i2c_rtc_get();
        struct tm rt = {};
        rt.tm_year = (int)r.year - 1900;
        rt.tm_mon = (int)r.month - 1;
        rt.tm_mday = r.day;
        rt.tm_hour = r.hour;
        rt.tm_min = r.minute;
        rt.tm_sec = r.second;
        time_t rtc_epoch = mktime(&rt);

        if (rtc_epoch < build_epoch) {
            ESP_LOGI(TAG, "RTC (%04d-%02d-%02d %02d:%02d:%02d) older than build "
                          "(%04d-%02d-%02d %02d:%02d:%02d), reseeding",
                     r.year, r.month, r.day, r.hour, r.minute, r.second,
                     b_y, b_mo, b_d, b_h, b_mi, b_s);
            i2c_rtc_setTime((uint16_t)b_y, (uint8_t)b_mo, (uint8_t)b_d,
                            (uint8_t)b_h, (uint8_t)b_mi, (uint8_t)b_s);
            r = i2c_rtc_get();
        }

        struct tm tm = {};
        tm.tm_year = (int)r.year - 1900;
        tm.tm_mon = (int)r.month - 1;
        tm.tm_mday = r.day;
        tm.tm_hour = r.hour;
        tm.tm_min = r.minute;
        tm.tm_sec = r.second;
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
                 r.year, r.month, r.day, r.hour, r.minute, r.second, (long long)t);
    }

    tz_apply_current();
    wifi_manager_init();

    if (webui_start() != ESP_OK) {
        ESP_LOGW(TAG, "webui_start failed");
    }

    show_main_ui(g_status_text);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
    cli_start();

    radio_engine_warm_at_boot();
    bg_fetcher_ensure();

    if (!g_cfg.wifi_autoconnect) {
        ESP_LOGI(TAG, "auto-connect: disabled in settings");
    } else {
        if (!g_cfg.last_ssid[0] && DEFAULT_WIFI_SSID[0]) {
            strncpy(g_cfg.last_ssid, DEFAULT_WIFI_SSID,
                    sizeof(g_cfg.last_ssid) - 1);
        }
        char pass[65] = {0};
        bool have_pass = cfg_get_ssid_pass(g_cfg.last_ssid, pass, sizeof(pass));
        if (!have_pass && DEFAULT_WIFI_SSID[0] && DEFAULT_WIFI_PASS[0] &&
            strcmp(g_cfg.last_ssid, DEFAULT_WIFI_SSID) == 0) {
            strncpy(pass, DEFAULT_WIFI_PASS, sizeof(pass) - 1);
            cfg_save_ssid_pass(g_cfg.last_ssid, pass);
            have_pass = true;
        }
        if (g_cfg.last_ssid[0] && have_pass) {
            ESP_LOGI(TAG, "auto-connect: %s (pass_len=%u)",
                     g_cfg.last_ssid, (unsigned)strlen(pass));
            wifi_connect(g_cfg.last_ssid, pass);
        } else {
            ESP_LOGI(TAG, "auto-connect: no credentials yet (use Settings -> Wi-Fi)");
        }
    }

    uint32_t heartbeat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)g_fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}