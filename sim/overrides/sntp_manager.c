/* sim/overrides/sntp_manager.c - SNTP 时间同步 no-op
 * PC 模拟器使用系统本地时间，无需 NTP 同步。
 */
#include "esp_log.h"
#include <time.h>

static const char *TAG = "sntp";

void sntp_manager_start(void) {
    /* PC 使用 localtime，无需初始化 NTP */
    ESP_LOGI(TAG, "sim sntp_manager_start (using system localtime)");
}
