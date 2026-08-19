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
#include "sntp_manager.h"
#include "system_monitor.h"
#include "board.h"
#include "network_init.h"
#include "ui.h"
#include "ui_helpers.h"
#include "ui_events.h"
#include "nas_event_loop.h"
#include "event_bus.h"
#include "rgb_nas_monitor.h"

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

/* 板级初始化进度 → 启动画面文本（board 层不直接依赖 UI，通过回调解耦） */
static void boot_status_cb(const char *line)
{
    char buf[256];
    ui_helpers_get_status_text(buf, sizeof(buf));
    int pos = (int)strlen(buf);
    if (pos >= (int)sizeof(buf) - 1) return;
    strncat(buf, line, sizeof(buf) - pos - 1);
    ui_helpers_set_status_text(buf);
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

    /* 硬件初始化（随板内聚，见 boards/<name>/board.c） */
    board_set_status_cb(boot_status_cb);
    board_init();

    network_init();

    nas_event_loop_start();

    ui_init();

    // 启动 UI 事件任务（消费 NAS 数据/WiFi 事件更新界面，此前遗漏导致屏幕数据不刷新）
    if (lvgl_lock(-1)) {
        ui_events_start();
        lvgl_unlock();
    }

    // 初始化 RGB LED NAS 监控
    rgb_nas_monitor_init();
    rgb_nas_monitor_start();

    cli_start();

    ESP_LOGI(TAG, "===== boot complete, entering main loop =====");
    system_monitor_start();
}


