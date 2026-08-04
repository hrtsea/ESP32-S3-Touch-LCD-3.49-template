#pragma once

#include "rgb_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 RGB LED NAS 监控模块
 *
 * 集成到事件总线，自动订阅 NAS 数据更新事件
 *
 * @return ESP_OK 成功
 */
esp_err_t rgb_nas_monitor_init(void);

/**
 * @brief 启动温度监控
 *
 * 开始监听 NAS 温度数据并自动更新 LED 颜色
 */
void rgb_nas_monitor_start(void);

/**
 * @brief 停止温度监控
 */
void rgb_nas_monitor_stop(void);

#ifdef __cplusplus
}
#endif