# ZotLab NAS Monitor — 全面规划与实施计划

> 项目：ESP32-S3 NAS 监控屏幕（兼容多种 NAS、动态硬盘槽位、PWM 风扇控制、详情页导航）
> 硬件平台：ESP32-S3 Touch LCD 3.49（640×172 横屏）
> 软件框架：ESP-IDF v5.5.4 + LVGL
> 创建日期：2026-07-18

---

## 一、项目目标

实现一个嵌入式 NAS 系统监控屏幕，具备：

1. **多 NAS 兼容**：支持 11 种 NAS 类型（Synology/QNAP/TrueNAS/FNOS/UnRAID/Linux-HTTP/Linux-Serial/Windows/Netdata/SNMP/Mock）
2. **多硬件兼容**：通过 UI 配置 SATA + M.2 硬盘数量，UI 自动按槽位数重建
3. **PWM 风扇控制**：5 点温控曲线、AUTO/MANUAL 模式、TACH 测速、停转告警、紧急温度保护
4. **设置界面**：NAS 类型/SATA-M.2 数量、WiFi 连接、风扇温控曲线（可点击图表编辑）
5. **详情页导航**：点击硬盘按钮 → 硬盘详情；点击 CPU/内存区域 → 系统详情
6. **覆盖式加载**：详情屏 `lv_scr_load_anim` 覆盖当前屏，手势右滑返回 Overview

---

## 二、现状与需求差距分析

### 2.1 已就绪基础设施 ✅

| 模块 | 文件 | 状态 |
|------|------|------|
| 配置结构 | `config/config.h` | ✅ `AppConfig.fan` 字段、`sata_disk_count`/`m2_disk_count`、`config_get_total_disk_slots()` |
| NVS 持久化 | `config/config.c` | ✅ `config_save_fan()`、`config_save_disk_config()` 全字段存储 |
| 事件 ID | `utils/event_bus.h` | ✅ 已添加 `EVENT_FAN_CONFIG_CHANGED`、`EVENT_FAN_STATUS_UPDATE` |
| 事件队列 | `utils/event_bus.c` | ✅ 纯队列模式，单任务消费 |
| 风扇数据结构 | `data/fan_control.h` | ✅ `FanMode`、`TempSource`、`FanCurvePoint`、`FanConfig`、`DEFAULT_FAN_CURVE` |
| NAS 数据 | `data/nas_data.h` | ✅ `NasDiskInfo`（含 disk_type/temp/health/size/used/rw）、`NasSystemInfo`（含 cpu_cores/load_avg/ram/swap/temp）、`FanStatus`、`NasData` |
| 数据源工厂 | `data/data_source.c` | ✅ 11 种 NAS 类型 VTable |
| Overview 屏 | `ui/screens/ui_Screen_Overview.c` | ✅ 动态 HDD 指示灯、CPU/Temp meter、内存/磁盘条、值变化检测 |
| Storage 屏 | `ui/screens/ui_Screen_Storage.c` | ✅ 动态 HDD 条布局（≤4 用 1 列，>4 用 2 列） |
| Settings 主屏 | `ui/screens/ui_Screen_Settings.c` | ✅ tabview，7 个标签页 |
| NAS 设置 Tab | `ui/screens/ui_Screen_Settings_NasTab.c` | ✅ NAS 类型选择、SATA/M.2 下拉联动、配置对话框 |
| UI 事件循环 | `ui/ui_events.c` | ✅ `task_ui_event_loop` 队列消费、`EVENT_DISK_CONFIG_CHANGED` 处理（重建 Overview/Storage） |
| PWM 参考 | `components/lcd_bl_pwm_bsp/lcd_bl_pwm_bsp.c` | ✅ LEDC 8bit 50KHz 模板 |
| 事件总线发布 | `config.c::config_save_disk_config` | ✅ 已发布 `EVENT_DISK_CONFIG_CHANGED` |

### 2.2 缺失部分 ❌

| 缺失项 | 说明 |
|--------|------|
| `fan_control.c` | 风扇控制实现（PWM 输出、曲线插值、TACH 测速、控制任务） |
| `EVENT_FAN_CONFIG_CHANGED` 发布 | `config_save_fan()` 末尾未发布事件 |
| FAN GPIO 宏 | `config.h` 未定义 `FAN_PWM_GPIO`、`FAN_TACH_GPIO`、`FAN_LEDC_TIMER`、`FAN_LEDC_CHANNEL` |
| `ui_Screen_Settings_FanTab.c/.h` | 风扇设置标签页（5 点可点击图表、模式切换、参数滑块） |
| `ui_Screen_DiskDetail.c/.h` | 硬盘详情屏（点击 HDD 进入，显示该盘全部信息） |
| `ui_Screen_SystemDetail.c/.h` | 系统详情屏（点击 CPU/内存进入，显示 CPU/内存/温度详情） |
| Overview 点击导航 | HDD 按钮未传索引；CPU meter/内存条未绑定点击 |
| Settings FanTab 集成 | `ui_Screen_Settings.c` 未调用 FanTab_init/cleanup |
| `ui.h` 声明 | 未包含 3 个新屏头文件 |
| `hw_init.c` 集成 | 未调用 `fan_control_init()` |
| `ui_events.c` 事件处理 | 未处理 `EVENT_FAN_CONFIG_CHANGED`、未实现 DiskDetail/SystemDetail 数据更新 |

---

## 三、新增/修改文件清单

### 3.1 新建文件（7 个）

| 路径 | 职责 |
|------|------|
| `main/data/fan_control.c` | 风扇控制实现 |
| `main/ui/screens/ui_Screen_Settings_FanTab.h` | FanTab 接口 |
| `main/ui/screens/ui_Screen_Settings_FanTab.c` | FanTab 实现（可点击图表） |
| `main/ui/screens/ui_Screen_DiskDetail.h` | DiskDetail 接口 |
| `main/ui/screens/ui_Screen_DiskDetail.c` | DiskDetail 实现 |
| `main/ui/screens/ui_Screen_SystemDetail.h` | SystemDetail 接口 |
| `main/ui/screens/ui_Screen_SystemDetail.c` | SystemDetail 实现 |

### 3.2 修改文件（8 个）

| 路径 | 修改内容 |
|------|----------|
| `main/config/config.h` | 添加 FAN GPIO/LEDC 宏定义 |
| `main/config/config.c` | `config_save_fan()` 末尾发布 `EVENT_FAN_CONFIG_CHANGED` |
| `main/ui/ui.h` | 包含 3 个新屏头文件 |
| `main/ui/screens/ui_Screen_Settings.h` | 包含 `ui_Screen_Settings_FanTab.h` |
| `main/ui/screens/ui_Screen_Settings.c` | 调用 `FanTab_init()`/`FanTab_cleanup()` |
| `main/ui/screens/ui_Screen_Overview.c` | HDD 按钮点击传索引（user_data）；CPU meter/内存条绑定点击事件 |
| `main/ui/ui_events.c` | 处理 `EVENT_FAN_CONFIG_CHANGED`；新增 HDD 点击/CPU 点击/内存点击回调路由到详情屏；详情屏数据更新 |
| `main/utils/hw_init.c` | 添加 `#include "fan_control.h"` 与 `fan_control_init()` 调用 |

---

## 四、详细设计

### 4.1 任务 1：`fan_control.c` 完整实现

**职责**：LEDC PWM 输出、PCNT 测速、曲线插值、AUTO 控制循环、MANUAL 直控、停转告警、紧急保护。

**对外 API**（在 `fan_control.h` 中追加声明）：

```c
void fan_control_init(void);                        // 初始化 LEDC + PCNT + 控制任务
void fan_control_set_pwm(uint8_t pct);              // 直接设置 PWM (0-100)
uint16_t fan_control_get_rpm(void);                 // 读取当前 RPM
uint8_t fan_control_get_current_pwm(void);          // 读取当前实际 PWM
int16_t fan_control_get_ctrl_temp(void);            // 读取当前控制温度
bool fan_control_is_stall_alarm(void);              // 停转告警
void fan_control_apply_config(const FanConfig *cfg); // 配置变更后重载
void fan_control_get_status(FanStatus *out);        // 填充 FanStatus 给 NasData
```

**GPIO 宏**（在 `config.h` 中添加，占位符，硬件确定后修改）：

```c
#ifndef FAN_PWM_GPIO
#define FAN_PWM_GPIO      4
#endif
#ifndef FAN_TACH_GPIO
#define FAN_TACH_GPIO     5
#endif
#ifndef FAN_LEDC_TIMER
#define FAN_LEDC_TIMER    LEDC_TIMER_2
#endif
#ifndef FAN_LEDC_CHANNEL
#define FAN_LEDC_CHANNEL  LEDC_CHANNEL_2
#endif
#define FAN_PWM_FREQ_HZ   25000      // 25KHz（风扇标准 PWM 频率）
#define FAN_PWM_RES_BIT   LEDC_TIMER_10_BIT  // 1024 级
#define FAN_PCNT_UNIT     PCNT_UNIT_0
#define FAN_PCNT_CHANNEL  PCNT_CHANNEL_0
#define FAN_RPM_SAMPLE_MS 1000       // 1 秒采样窗口
#define FAN_PULSES_PER_REV 2         // 风扇每转脉冲数（典型 2）
```

**实现要点**：

1. **LEDC 配置**：`LEDC_LOW_SPEED_MODE`、`FAN_LEDC_TIMER`、`FAN_LEDC_CHANNEL`、`FAN_PWM_GPIO`、25KHz、10bit
2. **PCNT 配置**：`PCNT_UNIT_0`、`PCNT_CHANNEL_0`、`FAN_TACH_GPIO`、下降沿计数、`pcnt_isr_register` + `pcnt_intr_enable`
3. **控制任务**：`fan_control_task`（栈 4096，优先级 2），1 秒循环：
   - 读取 PCNT 计数 → 计算 RPM = (count * 60) / (FAN_PULSES_PER_REV * sample_seconds)
   - 根据 `g_config.fan.mode`：
     - `FAN_MODE_MANUAL`：直接输出 `manual_pwm_pct`
     - `FAN_MODE_AUTO`：
       - 从 `event_bus_publish` 之外的渠道读取最新温度（订阅 `EVENT_NAS_DATA_UPDATE` 缓存最新温度）
       - 调用 `fan_control_eval_curve(temp, curve)` 计算目标 PWM
       - 应用滞后：若 `|target - current| < hysteresis`，保持当前值
       - 应用最小变化阈值：若 `|target - current| < min_change_pct`，保持
       - 紧急温度：若 `temp >= emergency_temp`，强制 100%
   - 调用 `ledc_set_duty` + `ledc_update_duty` 输出
   - 更新内部状态：`s_current_pwm`、`s_current_rpm`、`s_ctrl_temp`、`s_stall_alarm`
   - 停转检测：若 `pwm > min_pwm_pct` 且 `rpm == 0` 持续 `stall_detect_sec` 秒 → `s_stall_alarm = true`，发布 `EVENT_FAN_STATUS_UPDATE`
   - 每 5 秒发布 `EVENT_FAN_STATUS_UPDATE`，data_source 会将其合并到 NasData.fan
4. **曲线插值函数**：

```c
static uint8_t eval_curve(int16_t temp, const FanCurvePoint *curve) {
    if (temp <= curve[0].temp) return curve[0].pwm_pct;
    if (temp >= curve[FAN_CURVE_POINTS-1].temp) return curve[FAN_CURVE_POINTS-1].pwm_pct;
    for (int i = 0; i < FAN_CURVE_POINTS - 1; i++) {
        if (temp >= curve[i].temp && temp < curve[i+1].temp) {
            int16_t dt = curve[i+1].temp - curve[i].temp;
            int16_t dp = (int16_t)curve[i+1].pwm_pct - (int16_t)curve[i].pwm_pct;
            return curve[i].pwm_pct + (uint8_t)((dp * (temp - curve[i].temp)) / dt);
        }
    }
    return curve[FAN_CURVE_POINTS-1].pwm_pct;
}
```

5. **温度源选择**：根据 `temp_source`：
   - `TEMP_MAX_CPU_SYS`：max(temp_cpu, temp_sys)
   - `TEMP_AVG_CPU_SYS`：(temp_cpu + temp_sys) / 2
   - `TEMP_CPU_ONLY`：temp_cpu
   - `TEMP_SYS_ONLY`：temp_sys

**缓存最新温度**：fan_control 内部维护 `static int16_t s_last_temp_cpu, s_last_temp_sys`，通过 `event_bus_subscribe(EVENT_NAS_DATA_UPDATE, ...)` 更新。注意：fan_control 任务自身是消费者，不能阻塞在 event_bus_receive；改为在订阅回调中更新静态变量（事件总线队列模式已无同步回调，因此 fan_control 自己起一个 `fan_data_task` 调用 `event_bus_receive` 接收数据事件并更新缓存）。

---

### 4.2 任务 2：`ui_Screen_Settings_FanTab.c/.h`（可点击图表）

**接口**：

```c
void ui_Screen_Settings_FanTab_init(lv_obj_t *parent);
void ui_Screen_Settings_FanTab_cleanup(void);
void fan_tab_save(lv_event_t *e);
```

**布局**（在 tabview 内，可用高度约 132px，宽度 640px）：

```
┌──────────────────────────────────────────────────────────────────┐
│ [Mode: AUTO▼] [Source: MAX CPU/SYS▼] [Enable ☑]  [Apply]        │
│ ┌─────────────────────────────┐  ┌──────────────────────────┐  │
│ │                             │  │ PWM(%)                   │  │
│ │   风扇曲线图表              │  │ 100 ─ ─── ───── ───── ●  │  │
│ │   (320×100 canvas)          │  │  75 ──────────────────    │  │
│ │   ●温度-PWM 坐标系          │  │  50 ────── ● ────────     │  │
│ │   ●5 个可拖拽点              │  │  25 ● ────────            │  │
│ │                             │  │   0 ──────────────────    │  │
│ │                             │  │      25 35 45 55 65 75 85 │  │
│ └─────────────────────────────┘  │      Temp(°C)             │  │
│ ┌────────────────────────────────┐└──────────────────────────┘  │
│ │ P1: 25°C/25%  P2: 35°C/30% ...│ [Hysteresis: 3▼] [Min: 20▼]   │
│ └────────────────────────────────┘[Emerg: 55°C] [Ramp: 2000ms]  │
└──────────────────────────────────────────────────────────────────┘
```

**实现要点**：

1. **顶部控制条**（高度 30px）：Mode 下拉 / Source 下拉 / Enable 开关 / Apply 按钮
2. **曲线画布**（`lv_canvas`，尺寸 320×100）：
   - 绘制坐标系：X 轴 = 温度（0-85°C），Y 轴 = PWM（0-100%）
   - 绘制 5 条网格线
   - 绘制曲线：连接 5 个点的折线
   - 绘制 5 个点：直径 8px 的圆，颜色随选中态变化
3. **点击/拖拽交互**：
   - 绑定 `LV_EVENT_PRESSED` / `LV_EVENT_PRESSING` / `LV_EVENT_RELEASED`
   - PRESSED：检测点击位置是否在某点半径 6px 内，标记 `s_dragging_point_idx`
   - PRESSING：若 `s_dragging_point_idx >= 0`，更新该点坐标（X 限幅在 [前一点 temp + 1, 后一点 temp - 1]，Y 限幅 [0, 100]），重绘画布
   - RELEASED：清除 `s_dragging_point_idx`，更新下方数值显示
4. **右侧 PWM/Temp 参考标尺**：静态绘制
5. **底部参数行**：Hysteresis 滑块、Min PWM 滑块、Emerg Temp 数字、Ramp Time 数字
6. **Apply 按钮**：调用 `config_save_fan(&s_editing_cfg)` 触发 `EVENT_FAN_CONFIG_CHANGED`
7. **内部维护编辑副本**：`static FanConfig s_editing_cfg`，从 `g_config.fan` 初始化；图表绘制基于 `s_editing_cfg.curve`

**图表绘制函数**：

```c
static void draw_curve_canvas(void) {
    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x101010), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x404040);
    line_dsc.width = 1;
    // ... 绘制网格线
    // 绘制曲线
    line_dsc.color = lv_color_hex(0x40E0D0);
    line_dsc.width = 2;
    for (int i = 0; i < FAN_CURVE_POINTS - 1; i++) {
        lv_point_precise_t p1 = temp_pwm_to_pixel(curve[i].temp, curve[i].pwm_pct);
        lv_point_precise_t p2 = temp_pwm_to_pixel(curve[i+1].temp, curve[i+1].pwm_pct);
        lv_draw_line(&layer, &line_dsc, &p1, &p2);
    }
    // 绘制点
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        lv_point_precise_t p = temp_pwm_to_pixel(curve[i].temp, curve[i].pwm_pct);
        rect_dsc.radius = LV_RADIUS_CIRCLE;
        rect_dsc.bg_color = (i == s_dragging_point_idx) ? lv_color_hex(0xFFFF00) : lv_color_hex(0xFFFFFF);
        lv_area_t area = {.x1 = p.x - 4, .y1 = p.y - 4, .x2 = p.x + 4, .y2 = p.y + 4};
        lv_draw_rect(&layer, &rect_dsc, &area);
    }
    lv_canvas_finish_layer(s_canvas, &layer);
}
```

---

### 4.3 任务 3：`ui_Screen_DiskDetail.c/.h`（硬盘详情屏）

**接口**：

```c
void ui_Screen_DiskDetail_screen_init(uint8_t disk_index);
void ui_Screen_DiskDetail_screen_destroy(void);
void ui_Screen_DiskDetail_update_data(const NasData *data);  // 数据更新时调用
extern lv_obj_t *ui_Screen_DiskDetail;
```

**布局**（640×172）：

```
┌──────────────────────────────────────────────────────────────────┐
│ [<Back]  Disk Detail - HDD3                       [Refresh]      │ <- 35px
├──────────────────────────────────────────────────────────────────┤
│ Name: WD Red 4TB         Model: WD40EFRX                         │
│ Device: /dev/sda         Mount: /volume1                         │
│ Type: SATA  Slot: 3       Online: YES  Health: OK                 │
│ ┌─────────────────────────────────────────────────────────────┐  │
│ │ Usage: 2.8TB / 4.0TB (70%)                                  │  │ <- bar
│ └─────────────────────────────────────────────────────────────┘  │
│ Temp: 42°C    Read: 1.2MB/s    Write: 0.5MB/s                    │
└──────────────────────────────────────────────────────────────────┘
```

**实现要点**：

1. **进入时携带索引**：`ui_Screen_DiskDetail_screen_init(uint8_t disk_index)` 内部保存 `s_disk_index`
2. **顶部返回按钮**：点击 `lv_scr_load_anim(ui_Screen_Overview, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false)`
3. **手势返回**：绑定 `LV_EVENT_GESTURE`，右滑触发返回 Overview
4. **数据更新**：`ui_events.c` 在 `EVENT_NAS_DATA_UPDATE` 处理中，若 `ui_Screen_DiskDetail != NULL`，调用 `ui_Screen_DiskDetail_update_data(data)`
5. **健康状态颜色映射**：OK=绿、WARNING=橙、CRITICAL=红、UNKNOWN=灰
6. **类型标签**：根据 `disk->disk_type` 显示 "SATA"/"M.2"/"NVMe"
7. **容量自动单位**：< 1TB 显示 GB，≥ 1TB 显示 TB

---

### 4.4 任务 4：`ui_Screen_SystemDetail.c/.h`（系统详情屏）

**接口**：

```c
void ui_Screen_SystemDetail_screen_init(SystemDetailMode mode);  // mode: CPU/MEM/OVERVIEW
void ui_Screen_SystemDetail_screen_destroy(void);
void ui_Screen_SystemDetail_update_data(const NasData *data);
extern lv_obj_t *ui_Screen_SystemDetail;
```

**布局**（640×172，分 CPU 模式和 MEM 模式）：

**CPU 模式**：
```
┌──────────────────────────────────────────────────────────────────┐
│ [<Back]  CPU Detail - nas-server                  [Refresh]      │
├──────────────────────────────────────────────────────────────────┤
│ Total: 45%   Load: 0.85 / 0.62 / 0.41                            │
│ Cores: [42%][45%][48%][40%][50%][45%][42%][44%] (8 cores)       │ <- 迷你条
│ Temp CPU: 58°C   Temp SYS: 48°C                                  │
│ Uptime: 12d 5h 32m  Hostname: nas-server  Model: DS920+          │
└──────────────────────────────────────────────────────────────────┘
```

**MEM 模式**：
```
┌──────────────────────────────────────────────────────────────────┐
│ [<Back]  Memory Detail - nas-server               [Refresh]      │
├──────────────────────────────────────────────────────────────────┤
│ RAM: 9.2GB / 16GB (58%)  [████████░░░░░]                         │
│ Free: 6.8GB   Cached: 2.4GB                                      │
│ Swap: 0.5GB / 2GB (25%)  [██░░░░░░░░░]                           │
│ Disk: 8.2TB / 12TB (68%)  [████████░░░]                           │
└──────────────────────────────────────────────────────────────────┘
```

**实现要点**：

1. **mode 参数**：`typedef enum { SYS_DETAIL_CPU, SYS_DETAIL_MEM } SystemDetailMode`
2. **返回按钮与手势**：同 DiskDetail
3. **CPU 核心条**：根据 `cpu_core_count` 动态生成迷你 bar（每个宽 60px）
4. **负载平均**：3 个 1/5/15 分钟负载
5. **数据更新**：`ui_events.c` 在 `EVENT_NAS_DATA_UPDATE` 中，若 `ui_Screen_SystemDetail != NULL`，调用 `ui_Screen_SystemDetail_update_data(data)`

---

### 4.5 任务 5：基础设施修改

#### 4.5.1 `config.h` 添加 FAN GPIO 宏

在文件末尾 `config.h` 的 `#ifdef __cplusplus` 之前插入：

```c
/* ============================================================
 * 风扇硬件引脚定义（占位符，实际硬件确定后修改）
 * ============================================================ */
#include "driver/ledc.h"
#include "driver/pcnt.h"

#ifndef FAN_PWM_GPIO
#define FAN_PWM_GPIO      4
#endif
#ifndef FAN_TACH_GPIO
#define FAN_TACH_GPIO     5
#endif
#ifndef FAN_LEDC_TIMER
#define FAN_LEDC_TIMER    LEDC_TIMER_2
#endif
#ifndef FAN_LEDC_CHANNEL
#define FAN_LEDC_CHANNEL  LEDC_CHANNEL_2
#endif
#define FAN_PWM_FREQ_HZ   25000
#define FAN_PWM_RES_BIT   LEDC_TIMER_10_BIT
#define FAN_PCNT_UNIT     PCNT_UNIT_0
#define FAN_PCNT_CHANNEL  PCNT_CHANNEL_0
#define FAN_PULSES_PER_REV 2
#define FAN_RPM_SAMPLE_MS 1000
```

> **注意**：`#include "driver/ledc.h"` 与 `#include "driver/pcnt.h"` 移到 `fan_control.h` 中更合适，避免 config.h 被大量使用者间接包含驱动头。最终选择放在 `fan_control.h`。

#### 4.5.2 `config.c` 发布事件

在 `config_save_fan()` 末尾（`memcpy(&g_config.fan, fan, sizeof(FanConfig));` 之后）添加：

```c
    memcpy(&g_config.fan, fan, sizeof(FanConfig));

    event_bus_publish(EVENT_FAN_CONFIG_CHANGED, NULL, 0);
```

#### 4.5.3 `ui.h` 包含新头文件

在 `#include "screens/ui_Screen_WifiConfig.h"` 之后添加：

```c
#include "screens/ui_Screen_DiskDetail.h"
#include "screens/ui_Screen_SystemDetail.h"
#include "screens/ui_Screen_Settings_FanTab.h"
```

#### 4.5.4 `ui_Screen_Settings.h` 包含 FanTab

在 `#include "ui_Screen_Settings_GuideTab.h"` 之后添加：

```c
#include "ui_Screen_Settings_FanTab.h"
```

#### 4.5.5 `ui_Screen_Settings.c` 集成 FanTab

在 `ui_Screen_Settings_GuideTab_init(ui_Settings_Tabview_ConfigPanel);` 之后添加：

```c
    ui_Screen_Settings_FanTab_init(ui_Settings_Tabview_ConfigPanel);
```

在 `ui_Screen_Settings_GuideTab_cleanup();` 之后添加：

```c
    ui_Screen_Settings_FanTab_cleanup();
```

#### 4.5.6 `ui_Screen_Overview.c` 改造点击导航

**HDD 按钮传索引**：

将 `create_hdd_indicators()` 中的：

```c
lv_obj_add_event_cb(btn_hdd, ui_event_Screen_Overview_hdd_clicked, LV_EVENT_ALL, NULL);
```

改为：

```c
static uint8_t s_hdd_indices[MAX_DISKS];
s_hdd_indices[i] = i;
lv_obj_add_event_cb(btn_hdd, ui_event_Screen_Overview_hdd_clicked, LV_EVENT_CLICKED, &s_hdd_indices[i]);
```

**CPU meter 点击**：

在 `create_cpu_module()` 末尾添加：

```c
    lv_obj_add_flag(s_screen.meter_cpu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen.meter_cpu, ui_event_Screen_Overview_cpu_clicked, LV_EVENT_CLICKED, NULL);
```

**内存条点击**：

在 `create_mem_disk_module()` 的 `mem_container` 创建后添加：

```c
    lv_obj_add_flag(mem_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mem_container, ui_event_Screen_Overview_mem_clicked, LV_EVENT_CLICKED, NULL);
```

#### 4.5.7 `ui_events.c` 事件处理扩展

**新增头文件**：

```c
#include "screens/ui_Screen_DiskDetail.h"
#include "screens/ui_Screen_SystemDetail.h"
```

**新增回调**（在文件末尾或合适位置）：

```c
void ui_event_Screen_Overview_hdd_clicked(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    uint8_t *idx_ptr = (uint8_t *)lv_event_get_user_data(e);
    uint8_t disk_idx = idx_ptr ? *idx_ptr : 0;

    if (ui_Screen_DiskDetail == NULL) {
        ui_Screen_DiskDetail_screen_init(disk_idx);
    } else {
        // 已存在则更新索引
        ui_Screen_DiskDetail_screen_destroy();
        ui_Screen_DiskDetail_screen_init(disk_idx);
    }
    lv_scr_load_anim(ui_Screen_DiskDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_event_Screen_Overview_cpu_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_SystemDetail == NULL) {
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_CPU);
    }
    lv_scr_load_anim(ui_Screen_SystemDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_event_Screen_Overview_mem_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_Screen_SystemDetail == NULL) {
        ui_Screen_SystemDetail_screen_init(SYS_DETAIL_MEM);
    }
    lv_scr_load_anim(ui_Screen_SystemDetail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
```

**`EVENT_NAS_DATA_UPDATE` 处理扩展**：

在 `on_nas_data_update_evt()` 末尾添加：

```c
    if (ui_Screen_DiskDetail != NULL) {
        ui_Screen_DiskDetail_update_data(data);
    }
    if (ui_Screen_SystemDetail != NULL) {
        ui_Screen_SystemDetail_update_data(data);
    }
```

**`EVENT_FAN_CONFIG_CHANGED` 处理**：

在 switch-case 中添加：

```c
    case EVENT_FAN_CONFIG_CHANGED:
        fan_control_apply_config(&g_config.fan);
        break;
```

#### 4.5.8 `hw_init.c` 集成风扇初始化

在 `#include "audio_min.h"` 之后添加：

```c
#include "fan_control.h"
```

在 `hw_init()` 中，`[7/9] Audio MIDI` 之后、`[9/9] SD card + Buttons` 之前插入：

```c
    ESP_LOGI(TAG, "[8/9] Fan control (PWM + TACH)");
    fan_control_init();
    status_text_append("FAN OK\n");
```

> 将原 `[9/9] SD card + Buttons` 改为 `[10/10]`，调整序号注释。

---

## 五、实施顺序（依赖关系驱动）

| 步骤 | 任务 | 文件 | 依赖 |
|------|------|------|------|
| 1 | 基础设施 | `config.h`、`config.c`、`fan_control.h` | 无 |
| 2 | 风扇控制实现 | `fan_control.c` | 步骤 1 |
| 3 | FanTab UI | `ui_Screen_Settings_FanTab.c/.h` | 步骤 1 |
| 4 | DiskDetail UI | `ui_Screen_DiskDetail.c/.h` | 步骤 1 |
| 5 | SystemDetail UI | `ui_Screen_SystemDetail.c/.h` | 步骤 1 |
| 6 | Settings 集成 | `ui_Screen_Settings.c/.h` | 步骤 3 |
| 7 | Overview 改造 | `ui_Screen_Overview.c` | 步骤 4、5 |
| 8 | UI 事件扩展 | `ui_events.c`、`ui.h` | 步骤 3、4、5 |
| 9 | 硬件初始化集成 | `hw_init.c` | 步骤 2 |
| 10 | 编译验证 | — | 全部 |

---

## 六、假设与决策

1. **风扇 GPIO 占位**：`FAN_PWM_GPIO=4`、`FAN_TACH_GPIO=5`，使用 `#ifndef` 守卫允许 `user_config.h` 覆盖。硬件确定后修改即可。
2. **PWM 频率**：25KHz（风扇标准），10bit 分辨率（1024 级）。
3. **TACH 测速**：PCNT 外设 + 1 秒采样窗口 + 每转 2 脉冲（典型风扇）。
4. **停转告警**：PWM > min_pwm 且 RPM = 0 持续 `stall_detect_sec` 秒触发。
5. **详情屏导航**：覆盖式加载（不进 TileView），手势右滑或返回按钮回到 Overview。`N_TILES` 保持 4 不变。
6. **曲线编辑**：`lv_canvas` 绘制坐标系 + 5 个可拖拽点，点击/拖拽时实时更新编辑副本，Apply 时持久化。
7. **数据更新流向**：`event_bus_publish(EVENT_NAS_DATA_UPDATE)` → `ui_event_loop` 任务 → `on_nas_data_update_evt()` → 同步调用 `ui_Screen_DiskDetail_update_data()` / `ui_Screen_SystemDetail_update_data()`。
8. **FanConfig 编辑副本**：FanTab 维护 `static FanConfig s_editing_cfg`，仅 Apply 时调用 `config_save_fan()` 触发 `EVENT_FAN_CONFIG_CHANGED`。
9. **风扇任务温度缓存**：`fan_control.c` 内部起独立任务 `fan_data_task`（栈 3072）调用 `event_bus_receive` 消费 `EVENT_NAS_DATA_UPDATE` 更新温度缓存；`fan_control_task` 读取缓存进行控制。
10. **mock_client 测试**：确保 `disk_type`、`size_gb`、`read_kbps`、`write_kbps`、`cpu_cores[]`、`load_avg[]` 等字段被填充，便于详情屏验证。

---

## 七、验证步骤

### 7.1 编译验证

```powershell
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py build 2>&1 | Tee-Object -FilePath "build_log.txt"
```

**通过标准**：0 error，仅允许 warning（未使用变量等）。

### 7.2 烧录功能验证

```powershell
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py -p COM3 flash monitor
```

**验证清单**：

- [ ] 启动日志 `[8/10] Fan control (PWM + TACH)` 出现 `FAN OK`
- [ ] Overview 屏 HDD 按钮点击 → 进入 DiskDetail，显示该盘信息
- [ ] DiskDetail 右滑/返回按钮 → 回到 Overview
- [ ] Overview 屏 CPU meter 点击 → 进入 SystemDetail (CPU 模式)
- [ ] Overview 屏内存条点击 → 进入 SystemDetail (MEM 模式)
- [ ] Settings 屏出现 FanTab 标签
- [ ] FanTab 显示 5 点曲线图表，可拖拽点
- [ ] FanTab Apply 后日志显示 `fan_control_apply_config` 调用
- [ ] 修改 SATA/M.2 数量后 Overview/Storage HDD 数量同步变化
- [ ] 修改 NAS 类型后 data_source 切换客户端
- [ ] WiFi 连接成功后 IP 标签显示

### 7.3 风扇控制实测

- [ ] MANUAL 模式设 PWM=50%，万用表测 FAN_PWM_GPIO 输出约 50% 占空比
- [ ] AUTO 模式下温度变化时 PWM 按曲线变化
- [ ] 温度超过 emergency_temp 时 PWM 强制 100%
- [ ] 拔掉风扇（模拟停转）后 stall_alarm 触发
- [ ] TACH 接风扇后 RPM 显示正确（与风扇标称值 ±10%）

---

## 八、风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| GPIO 4/5 与其他外设冲突 | 使用 `#ifndef` 守卫，硬件确定后修改 `user_config.h` 覆盖 |
| LVGL 画布拖拽精度不足 | 增加 6px 命中半径；点坐标按温度/PWM 步进 1°C/1% 量化 |
| fan_control 任务栈溢出 | 栈分配 4096，启用 `uxTaskGetStackHighWaterMark` 监控 |
| PCNT ISR 与 LEDC 共用资源 | 使用独立 PCNT unit 和 LEDC timer/channel，避免与 LCD 背光冲突（LCD 用 TIMER_3/CHANNEL_1） |
| 详情屏返回时未销毁导致内存泄漏 | 返回 Overview 时调用 `_screen_destroy()` 释放控件 |
| FanTab 图表重绘频繁导致卡顿 | 仅在拖拽时重绘，RELEASED 时一次刷新；使用 `lv_obj_invalidate(s_canvas)` 而非全屏重绘 |
| 事件队列溢出（FanStatus 5 秒 1 次 + NasData 5 秒 1 次） | 队列长度 16 足够；事件合并处理 |

---

## 九、不在本计划范围

- 风扇曲线预设方案（静音/平衡/性能）— 后续迭代
- 多风扇支持（当前仅 1 路 PWM + 1 路 TACH）
- 硬盘 SMART 详细信息（仅显示 NasDiskInfo 已有字段）
- 历史温度图表（仅实时值）
- 风扇曲线云端同步
- WebUI 远程配置风扇（当前仅本地 UI）

---

## 十、参考文件路径

- 计划文档：`e:\zotlabnas-split\ESP32-S3-Touch-LCD-3.49-template\.trae\documents\nas_monitor_full_plan.md`
- 风扇数据结构：[fan_control.h](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/fan_control.h)
- 配置结构：[config.h](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/config/config.h)
- 事件总线：[event_bus.h](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/utils/event_bus.h)
- PWM 参考：[lcd_bl_pwm_bsp.c](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/components/lcd_bl_pwm_bsp/lcd_bl_pwm_bsp.c)
- Overview 现状：[ui_Screen_Overview.c](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/ui/screens/ui_Screen_Overview.c)
- UI 事件循环：[ui_events.c](file:///E:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/ui/ui_events.c)
