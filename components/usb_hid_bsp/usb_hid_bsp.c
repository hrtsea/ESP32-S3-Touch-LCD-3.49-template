#include "usb_hid_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "USB_HID";

// HID 键盘报告描述符
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0xFF,  //   Usage Maximum (255)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0xFF,  //   Logical Maximum (255)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x06,  //   Report Count (6)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0         // End Collection
};

// HID 键盘报告结构（8 字节）
typedef struct {
    uint8_t modifier;  // 修饰键（Shift/Ctrl/Alt 等）
    uint8_t reserved;  // 保留字节
    uint8_t key[6];    // 按键码（最多 6 个同时按下）
} hid_keyboard_report_t;

static hid_keyboard_report_t s_report = {0};
static SemaphoreHandle_t s_report_mutex = NULL;
static bool s_initialized = false;

// 简化的 USB HID 状态（实际实现需要更完整的 USB 协议栈）
static bool s_usb_connected = false;

esp_err_t usb_hid_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "USB HID already initialized");
        return ESP_OK;
    }

    s_report_mutex = xSemaphoreCreateMutex();
    if (!s_report_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_report, 0, sizeof(s_report));

    // TODO: 实际的 USB HID 初始化需要配置 USB Device Stack
    // 这里需要调用 ESP-IDF 的 USB 驱动 API
    // 由于 ESP-IDF 5.3 的 USB 支持仍在演进，这里提供简化接口
    // 完整实现需要：
    // 1. usb_device_init()
    // 2. 注册 HID 设备描述符
    // 3. 配置端点
    // 4. 处理 USB 事件

    ESP_LOGW(TAG, "============================================");
    ESP_LOGW(TAG, "USB HID component compiled (stub mode)");
    ESP_LOGW(TAG, "Full USB stack implementation needed");
    ESP_LOGW(TAG, "This component provides the API framework");
    ESP_LOGW(TAG, "============================================");

    s_initialized = true;
    ESP_LOGI(TAG, "USB HID initialized (stub mode)");

    return ESP_OK;
}

void usb_hid_send_key(uint8_t key_code, bool pressed)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "USB HID not initialized");
        return;
    }

    if (xSemaphoreTake(s_report_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex");
        return;
    }

    if (pressed) {
        // 添加按键到报告
        for (int i = 0; i < 6; i++) {
            if (s_report.key[i] == 0) {
                s_report.key[i] = key_code;
                break;
            }
        }
    } else {
        // 从报告中移除按键
        for (int i = 0; i < 6; i++) {
            if (s_report.key[i] == key_code) {
                s_report.key[i] = 0;
                // 移动后面的按键填补空位
                for (int j = i; j < 5; j++) {
                    s_report.key[j] = s_report.key[j + 1];
                }
                s_report.key[5] = 0;
                break;
            }
        }
    }

    // TODO: 实际发送到 USB 端点
    // usb_device_send(s_report);

    ESP_LOGD(TAG, "Key 0x%02X %s", key_code, pressed ? "pressed" : "released");

    xSemaphoreGive(s_report_mutex);
}

void usb_hid_click_key(uint8_t key_code, uint32_t press_duration_ms)
{
    usb_hid_send_key(key_code, true);
    vTaskDelay(pdMS_TO_TICKS(press_duration_ms));
    usb_hid_send_key(key_code, false);
}

bool usb_hid_is_connected(void)
{
    return s_initialized && s_usb_connected;
}

// 供 USB 协议栈回调的函数（完整实现需要）
void usb_hid_set_connected(bool connected)
{
    s_usb_connected = connected;
    ESP_LOGI(TAG, "USB connected: %s", connected ? "yes" : "no");
}