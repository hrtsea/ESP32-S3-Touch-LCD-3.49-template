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

#define BL_MAX_BRIGHTNESS 255u
#define TCA9554_POWER_DELAY_MS 50u
#define HEARTBEAT_INTERVAL_MS 2000u
#define TM_YEAR_OFFSET 1900
#define TM_MONTH_OFFSET 1

extern "C" const lv_font_t font_jbmono_24;
extern "C" const lv_font_t font_jbmono_48;
extern "C" const lv_font_t font_jbmono_64;
extern "C" const lv_font_t font_jbmono_96;

extern "C" void tz_apply_current(void);
extern "C" const char *tz_current_city_name(void);
extern "C" void disp_driver_init(void);

static int status_text_pos = 0;

static void status_text_append(const char *fmt, ...)
{
    if (status_text_pos >= (int)sizeof(g_status_text) - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(g_status_text + status_text_pos, 
                      sizeof(g_status_text) - status_text_pos, fmt, args);
    va_end(args);
    if (n > 0) {
        status_text_pos += n;
        if (status_text_pos >= (int)sizeof(g_status_text) - 1) {
            status_text_pos = (int)sizeof(g_status_text) - 1;
            g_status_text[status_text_pos] = '\0';
        }
    }
}

/**
 * @brief 获取 LCD 帧缓冲区快照（用于 WebUI 预览）
 * 
 * @param out 输出缓冲区指针
 * @param cap 输出缓冲区容量（字节）
 * @return 成功返回实际拷贝的字节数，失败返回负数
 */
extern "C" int webui_snapshot_fb(void *out, size_t cap)
{
    if (!out) return -1;
    size_t need = (size_t)UI_CANVAS_W * UI_CANVAS_H * 2;
    if (cap < need) return -1;
    
    if (!lvgl_lock(50)) return -1;
    
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || !disp->driver || !disp->driver->draw_buf) {
        lvgl_unlock();
        return -1;
    }
    const void *src = disp->driver->draw_buf->buf1;
    if (!src) {
        lvgl_unlock();
        return -1;
    }
    
    memcpy(out, src, need);
    lvgl_unlock();
    return (int)need;
}

static void log_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);
}

static void nvs_init(void)
{
    esp_err_t er = nvs_flash_init();
    if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        er = nvs_flash_init();
    }
    ESP_ERROR_CHECK(er);
}

static void hardware_init(void)
{
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

    ESP_LOGI(TAG, "[7/9] Audio (audio_min: ES8311 + I2S)");
    if (audio_min_init() == ESP_OK) {
        audio_min_set_volume(g_cfg.audio_volume);
        status_text_append("Audio OK\n");
    } else {
        status_text_append("Audio FAIL\n");
    }

    ESP_LOGI(TAG, "[8/9] SD card + Buttons");
    _sdcard_init();
    button_Init();
    status_text_append("SD/Btn OK");
}

static void time_init(void)
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
    ESP_LOGI(TAG, "RTC seed: %04d-%02d-%02d %02d:%02d:%02d UTC -> epoch %lld",
             rtc_time.year, rtc_time.month, rtc_time.day,
             rtc_time.hour, rtc_time.minute, rtc_time.second, 
             (long long)system_epoch);
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
    show_main_ui(g_status_text);
    ESP_LOGI(TAG, "===== All drivers initialized =====");
}

static void cli_init(void)
{
    cli_start();
}

static void heartbeat_loop(void)
{
    uint32_t heartbeat = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
        size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t freedma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        size_t freespi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "alive #%lu  frames=%lu  heap8=%u dma=%u spiram=%u",
                 (unsigned long)heartbeat++,
                 (unsigned long)g_fps_frame_count,
                 (unsigned)free8, (unsigned)freedma, (unsigned)freespi);
    }
}

/**
 * @brief 应用程序主入口
 * 
 * 负责初始化系统各模块，包括：
 * 1. 日志系统初始化
 * 2. NVS 存储初始化
 * 3. 配置加载与验证
 * 4. 时区设置
 * 5. 硬件驱动初始化（I2C、TCA9554、背光、LCD、RTC/IMU、ADC、音频、SD卡/按钮）
 * 6. 时间同步（RTC → 系统时间）
 * 7. 网络服务初始化（Wi-Fi、WebUI）
 * 8. UI 初始化
 * 9. CLI 初始化
 * 10. 后台任务启动（电台预热、背景图获取）
 * 11. Wi-Fi 自动连接
 * 12. 心跳循环
 */
extern "C" void app_main(void)
{
    log_init();
    nvs_init();
    cfg_load();
    
    const char *city_name = tz_current_city_name();
    const char *tz_posix = k_tz_cities[g_cfg.tz_idx].posix_tz;
    ESP_LOGI(TAG, "cfg: tz=%s (%s) bri=%u dim=%us off=%us last_ssid=%s",
             city_name ? city_name : "(unknown)", 
             tz_posix ? tz_posix : "(unknown)",
             g_cfg.brightness, g_cfg.dim_s, g_cfg.off_s,
             g_cfg.last_ssid[0] ? g_cfg.last_ssid : "(none)");
    ESP_LOGI(TAG, "===== 12_HelloWorld_Skeleton boot =====");
    ESP_LOGI(TAG, "H_RES=%d V_RES=%d  DMA=%d SPIRAM=%d",
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES,
             LVGL_DMA_BUFF_LEN, LVGL_SPIRAM_BUFF_LEN);

    status_text_append("Drivers:\n");
    hardware_init();

    time_init();
    tz_apply_current();
    network_init();
    ui_init();
    cli_init();

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

    heartbeat_loop();
}
