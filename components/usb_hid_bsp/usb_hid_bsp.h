#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief USB HID 初始化
 *
 * 初始化 USB HID 设备，模拟标准键盘
 *
 * @return ESP_OK 成功
 */
esp_err_t usb_hid_init(void);

/**
 * @brief 发送按键事件
 *
 * @param key_code HID 按键码 (如 HID_KEY_F13)
 * @param pressed true=按下, false=释放
 */
void usb_hid_send_key(uint8_t key_code, bool pressed);

/**
 * @brief 发送按键点击（按下+释放）
 *
 * @param key_code HID 按键码
 * @param press_duration_ms 按下持续时间（毫秒）
 */
void usb_hid_click_key(uint8_t key_code, uint32_t press_duration_ms);

/**
 * @brief HID 标准按键码定义
 */
#define HID_KEY_NONE        0x00
#define HID_KEY_F13         0x68
#define HID_KEY_F14         0x69
#define HID_KEY_F15         0x6A
#define HID_KEY_F16         0x6B
#define HID_KEY_F17         0x6C
#define HID_KEY_F18         0x6D
#define HID_KEY_F19         0x6E
#define HID_KEY_F20         0x6F
#define HID_KEY_F21         0x70
#define HID_KEY_F22         0x71
#define HID_KEY_F23         0x72
#define HID_KEY_F24         0x73

/**
 * @brief 检查 USB HID 是否已连接到主机
 *
 * @return true 已连接, false 未连接
 */
bool usb_hid_is_connected(void);