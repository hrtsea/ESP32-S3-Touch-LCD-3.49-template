/**
 * @file wifi_manager.h
 * @brief WiFi 管理层：库初始化 + esp_bus 事件桥 + 共享状态
 *
 * WiFi 的配置/连接/扫描/状态等操作直接调用 esp_wifi_config 库 API（wifi_cfg_*），
 * 本模块承担三部分职责：
 *  1) 初始化 esp_wifi_config 库（默认网络/重试/AP/HTTP 配置，network_init 阶段调用一次）；
 *  2) 事件桥：把 esp_bus 的 WiFi 事件转发到 event_bus（UI 事件驱动）；
 *  3) 共享状态：扫描结果缓存、连接发起时间、最近断开原因——
 *     这些是 UI 显示所需、库本身不提供的运行期状态。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_wifi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 esp_wifi_config 库并订阅 esp_bus WiFi 事件转发到 event_bus */
void wifi_manager_init(void);

/* ---- 扫描结果缓存（wifi_scan_start 发起并填充） ---- */
void wifi_scan_start(void);
uint16_t wifi_scan_count(void);
const wifi_scan_result_t *wifi_scan_ap(uint16_t idx);

/* ---- 连接发起时间（ms，供 UI 判断连接超时） ---- */
void wifi_mark_connect_attempted(void);
uint32_t wifi_connect_started_ms(void);

/* ---- 最近一次断开原因（来自 wifi:disconnected 事件） ---- */
void wifi_set_last_reason(uint8_t reason);
uint8_t wifi_get_last_reason(void);

#ifdef __cplusplus
}
#endif
