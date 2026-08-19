/* sim/overrides/hw_init.c - 硬件初始化 no-op */
#include "hw_init.h"

void system_time_init(void) {
    /* PC 模拟器无 RTC / 系统时间播种 */
}

void system_monitor_start(void) {
    /* PC 模拟器无系统监控任务 */
}
