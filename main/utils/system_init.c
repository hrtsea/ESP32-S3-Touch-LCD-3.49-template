#include "system_init.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "disp_driver.h"

/* ============================================================
 * 板级硬件初始化已下沉到 boards/<name>/board.c 的 board_init()；
 * 系统时间由 network 层 sntp_manager 管理（SNTP 同步 + 时区应用），
 * 不再做 RTC 播种（不依赖联网前的时间显示）。
 * 本文件仅保留系统监控：system_monitor_start —— 心跳监控（常驻）。
 * ============================================================ */

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
