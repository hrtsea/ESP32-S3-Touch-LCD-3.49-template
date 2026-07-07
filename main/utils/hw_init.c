#include "hw_init.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "ui.h"
#include "ui_helpers.h"
#include "esp_io_expander_tca9554.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "user_config.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"
#include "lcd_bl_pwm_bsp.h"
#include "adc_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "audio_min.h"

#include "app_cfg.h"
#include "disp_driver.h"

#define TM_YEAR_OFFSET 1900
#define TM_MONTH_OFFSET 1

static const char *TAG = "hw_init";

#define BL_MAX_BRIGHTNESS 255u

static void system_time_init(void);
#define TCA9554_POWER_DELAY_MS 50u

#define STATUS_TEXT_BUF_SIZE 256

static void status_text_append(const char *fmt, ...)
{
    char buf[STATUS_TEXT_BUF_SIZE];
    ui_helpers_get_status_text(buf, sizeof(buf));
    int pos = (int)strlen(buf);
    if (pos >= (int)sizeof(buf) - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, 
                      sizeof(buf) - pos, fmt, args);
    va_end(args);
    if (n > 0) {
        ui_helpers_set_status_text(buf);
    }
}

void hw_init(void)
{
    status_text_append("Drivers:\n");
    ESP_LOGI(TAG, "[1/9] I2C buses");
    i2c_master_Init();
    ESP_LOGI(TAG, "      esp_i2c_bus_handle=%p", esp_i2c_bus_handle);
    status_text_append("I2C OK\n");

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
        vTaskDelay(pdMS_TO_TICKS(TCA9554_POWER_DELAY_MS));
    }
    status_text_append("TCA9554 OK\n");

    ESP_LOGI(TAG, "[3/9] LCD backlight PWM (from cfg)");
    lcd_bl_pwm_bsp_init((uint16_t)(BL_MAX_BRIGHTNESS - g_cfg.brightness));
    status_text_append("BL OK\n");

    ESP_LOGI(TAG, "[4/9] LCD panel + LVGL");
    disp_driver_init();
    status_text_append("LCD/Touch OK\n");

    ESP_LOGI(TAG, "[5/9] RTC + IMU");
    i2c_rtc_setup();
    i2c_imu_setup();
    status_text_append("RTC/IMU OK\n");

    ESP_LOGI(TAG, "[6/9] ADC battery");
    adc_bsp_init();
    status_text_append("ADC OK\n");

    ESP_LOGI(TAG, "[7/9] Audio MIDI (ES8311 + ES7210 + I2S TDM)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        status_text_append("MIDI OK\n");
    } else {
        status_text_append("MIDI FAIL\n");
    }

    ESP_LOGI(TAG, "[9/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    status_text_append("SD/Btn OK");
    system_time_init();
}

static void system_time_init(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t build_epoch = (time_t)BUILD_EPOCH_UTC;
    struct tm build_time = {};
    gmtime_r(&build_epoch, &build_time);
    int build_year = build_time.tm_year + TM_YEAR_OFFSET;
    int build_month = build_time.tm_mon + TM_MONTH_OFFSET;
    int build_day = build_time.tm_mday;
    int build_hour = build_time.tm_hour;
    int build_min = build_time.tm_min;
    int build_sec = build_time.tm_sec;

    RtcDateTime_t rtc_time = i2c_rtc_get();
    struct tm rtc_tm = {};
    rtc_tm.tm_year = (int)rtc_time.year - TM_YEAR_OFFSET;
    rtc_tm.tm_mon = (int)rtc_time.month - TM_MONTH_OFFSET;
    rtc_tm.tm_mday = rtc_time.day;
    rtc_tm.tm_hour = rtc_time.hour;
    rtc_tm.tm_min = rtc_time.minute;
    rtc_tm.tm_sec = rtc_time.second;
    time_t rtc_epoch = mktime(&rtc_tm);

    bool need_reseed = (rtc_epoch < build_epoch);
    if (need_reseed) {
        ESP_LOGI(TAG, "RTC (%04d-%02d-%02d %02d:%02d:%02d) older than build "
                      "(%04d-%02d-%02d %02d:%02d:%02d), reseeding",
                 rtc_time.year, rtc_time.month, rtc_time.day,
                 rtc_time.hour, rtc_time.minute, rtc_time.second,
                 build_year, build_month, build_day,
                 build_hour, build_min, build_sec);
        i2c_rtc_setTime((uint16_t)build_year, (uint8_t)build_month, (uint8_t)build_day,
                        (uint8_t)build_hour, (uint8_t)build_min, (uint8_t)build_sec);
        rtc_time = i2c_rtc_get();
    }

    struct tm system_tm = {};
    system_tm.tm_year = (int)rtc_time.year - TM_YEAR_OFFSET;
    system_tm.tm_mon = (int)rtc_time.month - TM_MONTH_OFFSET;
    system_tm.tm_mday = rtc_time.day;
    system_tm.tm_hour = rtc_time.hour;
    system_tm.tm_min = rtc_time.minute;
    system_tm.tm_sec = rtc_time.second;
    time_t system_epoch = mktime(&system_tm);
    struct timeval tv = { .tv_sec = system_epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    tz_apply_current();
    ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.minute, rtc_time.second,
             (long long)system_epoch);
}

static const char *TAG_SYSMON = "sysmon";

#define HEARTBEAT_INTERVAL_MS 2000u

static void heartbeat_loop(void)
{
    uint32_t heartbeat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG_SYSMON, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)g_fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}

void system_monitor_start(void)
{
    heartbeat_loop();
}
