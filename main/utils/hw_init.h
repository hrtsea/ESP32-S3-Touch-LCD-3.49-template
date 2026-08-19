#ifndef HW_INIT_H
#define HW_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

void system_time_init(void);      /* 读 RTC 播种系统时间 + 应用时区（main 层） */
void system_monitor_start(void);  /* 心跳监控（常驻） */

#ifdef __cplusplus
}
#endif

#endif
