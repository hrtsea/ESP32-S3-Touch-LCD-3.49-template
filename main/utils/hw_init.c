#include "hw_init.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_equipment.h"
#include "app_cfg.h"
#include "i18n.h"
#include "disp_driver.h"

#define TM_YEAR_OFFSET 1900
#define TM_MONTH_OFFSET 1

static const char *TAG = "hw_init";

/* ============================================================
 * 板级硬件初始化已下沉到 boards/<name>/board.c 的 board_init()
 * （I2C / TCA9554 / 背光 / 显示+LVGL / RTC / IMU / ADC / 音频 /
 *  风扇 / SD / 按键，按 periph 能力裁剪）。
 * 本文件仅保留系统级初始化：
 *   system_time_init()   —— 读 RTC 播种系统时间 + 应用时区
 *   system_monitor_start —— 心跳监控（常驻）
 * ============================================================ */

void system_time_init(void)
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
