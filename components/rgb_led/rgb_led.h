#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGB LED 动画模式
 *
 * 与 Zettlab 协议完全兼容
 */
typedef enum {
    RGB_MODE_OFF      = 0,  // 关闭
    RGB_MODE_BREATHE  = 1,  // 呼吸效果
    RGB_MODE_FLOW     = 2,  // 颜色流动
    RGB_MODE_FOUNTAIN = 3,  // 喷泉效果
    RGB_MODE_GRADIENT = 4,  // 渐变
    RGB_MODE_FLICKER  = 5,  // 闪烁
    RGB_MODE_LIGHT    = 6,  // 固定颜色
    RGB_MODE_TEMP_MAP = 7   // 温度映射（ESP32 扩展模式）
} RgbMode;

/**
 * @brief RGB 颜色结构体
 */
typedef struct {
    uint8_t r;  // 红色分量 (0-255)
    uint8_t g;  // 绿色分量 (0-255)
    uint8_t b;  // 蓝色分量 (0-255)
} RgbColor;

/**
 * @brief RGB LED 配置
 */
typedef struct {
    RgbMode mode;           // 当前模式
    RgbColor start_color;   // 起始颜色
    RgbColor end_color;     // 结束颜色
    uint8_t speed;          // 动画速度 (0-255)
    float brightness;       // 亮度 (0.0-1.0)
    bool enabled;           // 启用状态

    // 温度映射参数（仅 RGB_MODE_TEMP_MAP 模式）
    float temp_min;         // 最低温度阈值
    float temp_max;         // 最高温度阈值
    bool use_cpu_temp;      // true=CPU温度, false=系统温度
} RgbLedConfig;

/**
 * @brief RGB LED 状态
 */
typedef struct {
    RgbColor current_color;  // 当前颜色
    RgbMode current_mode;     // 当前模式
    bool animation_active;    // 动画是否运行中
} RgbLedStatus;

/**
 * @brief 初始化 RGB LED
 *
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_init(void);

/**
 * @brief 设置固定颜色
 *
 * @param color RGB 颜色值
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_set_color(RgbColor color);

/**
 * @brief 设置动画模式
 *
 * @param mode 动画模式
 * @param start 起始颜色
 * @param end 结束颜色
 * @param speed 动画速度
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_set_mode(RgbMode mode, RgbColor start, RgbColor end, uint8_t speed);

/**
 * @brief 应用配置
 *
 * @param config RGB LED 配置
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_apply_config(const RgbLedConfig *config);

/**
 * @brief 获取当前状态
 *
 * @param status 状态输出
 */
void rgb_led_get_status(RgbLedStatus *status);

/**
 * @brief 温度映射颜色
 *
 * 根据 CPU 或系统温度计算 RGB 颜色
 *
 * @param temp 当前温度
 * @param min_temp 最低温度阈值
 * @param max_temp 最高温度阈值
 * @return RgbColor 计算得到的颜色
 */
RgbColor rgb_led_temp_to_color(float temp, float min_temp, float max_temp);

/**
 * @brief 停止所有动画
 */
void rgb_led_stop(void);

#ifdef __cplusplus
}
#endif