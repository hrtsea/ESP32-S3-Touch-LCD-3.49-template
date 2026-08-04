# RGB LED NAS 监控集成指南

## 概述

RGB LED 功能已成功集成到 ESP32-S3 NAS 监控系统中，实现与 Zettlab NAS 相同的智能温度映射功能。

---

## 功能特性

### 1. **智能温度映射**

- 实时监控 NAS CPU 或系统温度
- 自动映射到 RGB LED 颜色（蓝色 → 红色）
- 无需 PC 参与，ESP32-S3 独立运行

### 2. **多种动画模式**

- **温度映射模式（推荐）**：根据温度自动调整颜色
- **固定颜色**：手动设置 RGB 值
- **呼吸效果**：柔和的亮度渐变
- **颜色渐变**：起始→结束颜色平滑过渡
- **关闭模式**：LED 完全关闭

### 3. **灵活配置**

- 温度范围可配置（默认 35-85°C）
- 亮度可调节（默认 25%）
- 温度源可选（CPU 或系统温度）

---

## 硬件连接

### RGB LED 模块接线

```
ESP32-S3 DevKit
    │
    ├── GPIO 4  ── RGB LED 红色通道
    ├── GPIO 5  ── RGB LED 绿色通道
    ├── GPIO 6  ── RGB LED 蓝色通道
    │
    └── GND     ── RGB LED 公共地
```

**注意**：
- 普通 RGB LED 需要每个通道串联限流电阻（推荐 220Ω）
- WS2812B 等智能 LED 只需 1 根数据线（需修改驱动）

---

## 软件架构

### 文件结构

```
components/rgb_led/
├── CMakeLists.txt        # 组件构建配置
├── rgb_led.h             # API 头文件
└── rgb_led.c             # 实现文件（硬件驱动）

main/utils/
├── rgb_nas_monitor.h     # NAS 监控 API
└── rgb_nas_monitor.c     # 事件总线集成
```

### 系统集成

```
┌─────────────────────────────────────┐
│  NAS 数据源 (网络/串口)             │
│  └─ 发布 EVENT_NAS_DATA_UPDATE      │
└──────────────┼──────────────────────┘
               │
               ▼
┌──────────────┴──────────────────────┐
│  rgb_nas_monitor                    │
│  ├─ 订阅 NAS 数据事件               │
│  ├─ 提取温度数据                    │
│  └─ 更新 RGB LED                    │
└──────────────┼──────────────────────┘
               │
               ▼
┌──────────────┴──────────────────────┐
│  rgb_led                            │
│  ├─ LEDC PWM 硬件驱动               │
│  ├─ 温度→颜色映射算法               │
│  └─ 动画任务                        │
└─────────────────────────────────────┘
```

---

## API 使用

### 基础 API

```c
#include "rgb_led.h"

// 初始化
esp_err_t rgb_led_init(void);

// 设置固定颜色
RgbColor color = {255, 0, 0};  // 红色
rgb_led_set_color(color);

// 设置动画模式
RgbColor start = {255, 0, 0};  // 红色
RgbColor end   = {0, 0, 255};  // 蓝色
rgb_led_set_mode(RGB_MODE_GRADIENT, start, end, 10);

// 温度映射（自动模式）
RgbLedConfig config = {
    .mode = RGB_MODE_TEMP_MAP,
    .temp_min = 35.0f,
    .temp_max = 85.0f,
    .brightness = 0.25f,
    .use_cpu_temp = true
};
rgb_led_apply_config(&config);
```

### NAS 监控 API

```c
#include "rgb_nas_monitor.h"

// 初始化并自动订阅事件
rgb_nas_monitor_init();

// 启动温度监控
rgb_nas_monitor_start();

// 停止监控
rgb_nas_monitor_stop();
```

---

## 配置参数

### 温度范围

```c
config.temp_min = 35.0f;  // 最低温度（显示蓝色）
config.temp_max = 85.0f;  // 最高温度（显示红色）
```

### 亮度设置

```c
config.brightness = 0.25f;  // 25% 亮度（推荐）
```

**亮度推荐值**：
- 高亮度：`0.5f` (50%) - 夜间明显，白天清晰
- 中等亮度：`0.25f` (25%) - 平衡可见性和功耗
- 低亮度：`0.1f` (10%) - 夜间不刺眼

### 温度源选择

```c
config.use_cpu_temp = true;   // 使用 CPU 温度
config.use_cpu_temp = false;  // 使用系统温度
```

---

## 使用示例

### 示例 1：默认温度监控

```c
// 在 main.cpp 中已集成，无需额外代码
// 默认配置：
// - 温度范围：35-85°C
// - 亮度：25%
// - 温度源：CPU 温度
```

### 示例 2：自定义温度范围

```c
// 修改 rgb_nas_monitor.c 中的配置
RgbLedConfig config = {
    .mode = RGB_MODE_TEMP_MAP,
    .brightness = 0.3f,    // 30% 亮度
    .temp_min = 40.0f,     // 最低 40°C
    .temp_max = 90.0f,     // 最高 90°C
    .use_cpu_temp = true
};
```

### 示例 3：固定颜色

```c
// 设置为固定绿色
RgbColor green = {0, 255, 0};
rgb_led_set_color(green);
```

### 示例 4：呼吸效果

```c
RgbColor blue = {0, 0, 255};
rgb_led_set_mode(RGB_MODE_BREATHE, blue, blue, 10);
```

---

## LEDC 资源分配

### PWM 定时器和通道

| 功能 | Timer | Channel | GPIO |
|------|-------|---------|------|
| **RGB LED R** | LEDC_TIMER_1 | LEDC_CHANNEL_3 | GPIO 4 |
| **RGB LED G** | LEDC_TIMER_1 | LEDC_CHANNEL_4 | GPIO 5 |
| **RGB LED B** | LEDC_TIMER_1 | LEDC_CHANNEL_5 | GPIO 6 |
| **LCD 背光** | LEDC_TIMER_3 | — | — |
| **风扇 PWM** | LEDC_TIMER_2 | — | — |

**注意**：RGB LED 使用 LEDC_TIMER_1，避免与 LCD 背光和风扇冲突。

---

## 调试方法

### 启用调试日志

```c
// 在 rgb_led.c 中
esp_log_level_set("RGB_LED", ESP_LOG_DEBUG);
esp_log_level_set("RGB_NAS", ESP_LOG_DEBUG);
```

### 检查输出

```bash
# 编译并烧录
idf.py build flash monitor

# 查看日志
I (1234) RGB_LED: Initialized (R=4, G=5, B=6, mode=7)
I (1235) RGB_NAS: RGB NAS monitor initialized
I (1236) RGB_NAS: Monitoring started (temp range: 35.0-85.0°C)
D (1240) RGB_NAS: NAS temp updated: 62.5°C (source=CPU)
D (1241) RGB_LED: Temp 62.5°C -> RGB(12,0,8)
```

---

## 性能优化

### 1. **避免频繁更新**

```c
// 温度映射模式每 2 秒更新一次
vTaskDelay(pdMS_TO_TICKS(2000));
```

### 2. **亮度限制**

```c
// 限制最大亮度，避免过载
if (value > 255 * s_config.brightness) {
    value = 255 * s_config.brightness;
}
```

### 3. **线程安全**

```c
// 使用互斥锁保护共享状态
xSemaphoreTake(s_state_mux, portMAX_DELAY);
// ... 操作状态 ...
xSemaphoreGive(s_state_mux);
```

---

## 故障排查

### 问题 1：LED 不亮

**可能原因**：
1. GPIO 引脚错误
2. 亮度设置为 0
3. LED 灯珠损坏

**解决方法**：
```c
// 检查 GPIO 定义
#ifndef RGB_LED_R_GPIO
#define RGB_LED_R_GPIO   4
#endif

// 检查亮度
ESP_LOGI(TAG, "Brightness: %.2f", s_config.brightness);

// 手动测试
rgb_led_set_color((RgbColor){255, 255, 255});
```

### 问题 2：颜色不符合预期

**可能原因**：
- 温度范围设置不合理
- 温度源选择错误

**解决方法**：
```c
// 检查当前温度
ESP_LOGI(TAG, "Current temp: %.1f°C", s_current_temp);

// 检查温度范围
ESP_LOGI(TAG, "Temp range: %.1f-%.1f°C", s_config.temp_min, s_config.temp_max);
```

### 问题 3：编译错误

**可能原因**：
- CMakeLists.txt 未添加依赖
- 头文件路径错误

**解决方法**：
```bash
# 清理并重新编译
idf.py fullclean
idf.py build
```

---

## 与 Zettlab 对比

| 特性 | Zettlab NAS | ESP32-S3 项目 |
|------|-------------|---------------|
| **温度源** | CPU Package + HDD | CPU + 系统温度 |
| **更新频率** | 20 秒 | 2 秒 ⭐ |
| **亮度控制** | 通过 RGB 值缩放 | 专用亮度参数 ⭐ |
| **可视化配置** | 无 | LCD 触摸屏 ⭐ |
| **独立运行** | 需要 PC Python 脚本 | 完全独立 ⭐ |
| **实时显示** | 无 | LCD 实时监控 ⭐ |

---

## 未来扩展

### 1. **UI 配置界面**

创建 Settings RGB Tab，允许用户通过触摸屏配置：
- 温度范围
- 亮度
- 动画模式
- 固定颜色

### 2. **更多动画模式**

实现 Zettlab 协议中的所有模式：
- 颜色流动（FLOW）
- 喷泉效果（FOUNTAIN）
- 闪烁效果（FLICKER）

### 3. **告警模式**

当温度超过阈值时：
- LED 快速闪烁
- 颜色变为红色
- 发送通知事件

---

## 总结

RGB LED 功能已完美集成到 NAS 监控系统中，实现了：

✅ 智能温度映射  
✅ 多种动画模式  
✅ 事件总线集成  
✅ 线程安全设计  
✅ 灵活配置参数  

**无需任何外部依赖，ESP32-S3 独立运行，实时监控 NAS 温度状态！**