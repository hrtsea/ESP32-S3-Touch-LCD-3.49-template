# ZotLab NAS Monitor - 项目Wiki

## 一、项目概述

ZotLab NAS Monitor 是一款基于 ESP32-S3 的 NAS 监控设备固件，具备以下核心功能：

| 功能模块 | 说明 |
|---------|------|
| **NAS 数据监控** | 支持 Synology、QNAP、TrueNAS、Netdata 等多种 NAS 类型的数据采集和展示 |
| **WiFi 管理** | 自动连接、漫游切换、SoftAP 配网 |
| **UI 界面** | 基于 LVGL 的触摸屏界面，包含 Overview、Settings、Storage 等屏幕 |
| **时间同步** | RTC 硬件时钟 + SNTP 网络同步 |
| **背景管理** | 支持深色/浅色/图片/纯色多种背景模式 |
| **WebUI** | 网页配网和参数设置界面 |

**硬件平台**：ESP32-S3 + 3.49" SPI LCD + CST920 触摸

**软件框架**：ESP-IDF v5.5.4 + LVGL

---

## 二、架构设计

### 2.1 分层架构

```
┌─────────────────────────────────────────────┐
│              UI 层 (ui/)                     │
│  ui.c / ui_events.c / ui_helpers.c / screens│
├─────────────────────────────────────────────┤
│            业务逻辑层 (data/)                │
│  nas_event_loop / data_source / clients     │
├─────────────────────────────────────────────┤
│           网络层 (network/)                  │
│  wifi_manager / wifi_provision / sntp       │
├─────────────────────────────────────────────┤
│           配置层 (config/)                   │
│  app_cfg (NVS 持久化)                       │
├─────────────────────────────────────────────┤
│           工具层 (utils/)                    │
│  event_bus / bg_fetcher / hw_init / cli     │
├─────────────────────────────────────────────┤
│         驱动层 (components/)                 │
│  I2C / LCD / Touch / Audio / ADC / Button   │
└─────────────────────────────────────────────┘
```

### 2.2 事件驱动架构

项目采用 **事件总线（Event Bus）** 作为模块间通信的核心机制：

| 事件类型 | 触发时机 | 订阅者 |
|---------|---------|--------|
| `EVENT_WIFI_CONNECTED` | WiFi 获取 IP | nas_event_loop, ui_events |
| `EVENT_WIFI_DISCONNECTED` | WiFi 断开 | nas_event_loop, ui_events |
| `EVENT_NAS_DATA_UPDATE` | NAS 数据更新 | ui_events |
| `EVENT_CFG_CHANGED` | 配置变更 | ui_events |
| `EVENT_CLOCK_BG_CHANGED` | 背景变更 | ui_clock |
| `EVENT_TILE_CHANGED` | Tile 切换 | ui_events |
| `EVENT_BACKLIGHT_CHANGED` | 背光变更 | ui_events |

**事件总线实现**：[event_bus.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/event_bus.c)

---

## 三、目录结构

### 3.1 main/ 目录

```
main/
├── config/              # 配置管理
│   ├── app_cfg.c/h      # 应用配置（NVS 持久化）
│   ├── config.c/h       # NAS 配置
│   └── app_info.h       # 应用信息
├── data/                # 数据采集
│   ├── client/          # 数据源客户端
│   │   ├── api_client.c/h
│   │   ├── synology_client.c/h
│   │   ├── qnap_client.c/h
│   │   ├── truenas_client.c/h
│   │   ├── netdata_client.c/h
│   │   ├── snmp_client.c/h
│   │   ├── serial_client.c/h
│   │   └── mock_client.c/h
│   ├── data_source.c/h  # 数据源工厂（VTable 模式）
│   ├── nas_data.h       # NAS 数据结构定义
│   ├── nas_data_json.c/h # JSON 解析
│   ├── nas_event_loop.c/h # 数据采集事件循环
│   └── fan_control.h    # 风扇控制接口
├── drivers/             # 显示驱动
│   └── disp_driver.c/h  # LCD 显示驱动（含 LVGL 适配）
├── fonts/               # 字体文件
├── network/             # 网络模块
│   ├── wifi_manager.c/h # WiFi 管理（STA/AP/漫游）
│   ├── wifi_provision.c/h # SoftAP 配网
│   ├── wifi_provision_dns.c/h # DNS 劫持
│   ├── wifi_provision_http.c/h # HTTP 配网服务
│   └── sntp_manager.c/h # SNTP 时间同步
├── ui/                  # UI 模块
│   ├── fonts/           # UI 字体
│   ├── images/          # UI 图片资源
│   ├── screens/         # 屏幕定义
│   │   ├── ui_Screen_Boot.c/h
│   │   ├── ui_Screen_Overview.c/h
│   │   ├── ui_Screen_Settings.c/h
│   │   ├── ui_Screen_Settings_WifiTab.c/h
│   │   ├── ui_Screen_Settings_NasTab.c/h
│   │   ├── ui_Screen_Settings_RegionTab.c/h
│   │   ├── ui_Screen_Settings_ScreenTab.c/h
│   │   ├── ui_Screen_Settings_StationTab.c/h
│   │   ├── ui_Screen_Settings_MusicTab.c/h
│   │   ├── ui_Screen_Settings_GuideTab.c/h
│   │   ├── ui_Screen_Storage.c/h
│   │   └── ui_Screen_WifiConfig.c/h
│   ├── ui.c/h           # UI 主入口
│   ├── ui_events.c/h    # UI 事件处理
│   └── ui_helpers.c/h   # UI 状态管理 + 辅助函数
├── utils/               # 工具模块
│   ├── bg_fetcher.c/h   # 背景图片获取
│   ├── cli.c/h          # 命令行接口
│   ├── event_bus.c/h    # 事件总线
│   ├── http_timer.c/h   # HTTP 请求定时器
│   ├── hw_init.c/h      # 硬件初始化
│   ├── i18n.c/h         # 国际化
│   ├── i18n_strings.h   # 多语言字符串
│   ├── landmask.h       # 占位掩码
│   ├── system_monitor.c/h # 系统监控
│   ├── theme.c/h        # 主题管理
│   ├── tz_cities.h      # 时区城市列表
│   └── tz_utils.c/h     # 时区工具
├── CMakeLists.txt
├── idf_component.yml
├── main.cpp             # 入口文件
├── user_config.h        # 用户配置
└── wifi_secret.example.h # WiFi 凭证示例
```

### 3.2 components/ 目录

| 组件 | 说明 |
|-----|------|
| `adc_bsp` | ADC 电池检测 |
| `audio_bsp` | 音频 BSP（ES8311 DAC + ES7210 ADC） |
| `audio_min` | 简化音频接口 |
| `button_bsp` | 按钮驱动 |
| `codec_board` | 编解码板驱动 |
| `i2c_bsp` | I2C 总线驱动 |
| `i2c_equipment` | I2C 设备（RTC/IMU/触摸） |
| `lcd_bl_pwm_bsp` | LCD 背光 PWM |
| `lvgl` | LVGL 图形库 |
| `SensorLib` | 传感器库（触摸/CST9xx/BMA423） |

---

## 四、启动流程

### 4.1 app_main() 执行顺序

```
app_main()
├── 1. log_init()           # 日志级别配置
├── 2. app_cfg_load()       # NVS 配置加载（触发 event_bus 懒初始化）
├── 3. hw_init()            # 硬件初始化（9步）
│   ├── I2C 总线
│   ├── TCA9554 电源扩展
│   ├── LCD 背光 PWM
│   ├── LCD 面板 + LVGL
│   ├── RTC + IMU
│   ├── ADC 电池检测
│   ├── Audio MIDI
│   ├── SD卡 + Buttons
│   └── system_time_init()  # RTC 时间初始化 + 时区设置
├── 4. network_init()       # 网络初始化
│   ├── wifi_manager_init() # WiFi STA 初始化 + 自动连接
│   ├── wifi_provision_init() # SoftAP 配网初始化
│   └── webui_start()       # WebUI 启动
├── 5. nas_event_loop_start() # NAS 数据采集循环启动
├── 6. http_timer_init() / http_timer_start() # HTTP 请求定时器
├── 7. ui_init()            # UI 初始化
│   ├── 创建 FPS 标签
│   ├── 创建 Boot 屏幕
│   └── bg_fetcher_ensure() # 背景获取
├── 8. cli_start()          # 命令行接口启动
└── 9. system_monitor_start() # 系统监控心跳（阻塞）
```

### 4.2 WiFi 连接流程

```
wifi_manager_init()
├── esp_netif_init()
├── esp_event_loop_create_default()
├── esp_wifi_init()
├── esp_wifi_set_mode(WIFI_MODE_STA)
├── 注册事件处理器
│   ├── WIFI_EVENT_STA_DISCONNECTED → 自动重连/漫游扫描
│   ├── WIFI_EVENT_STA_CONNECTED → 更新状态
│   ├── WIFI_EVENT_SCAN_DONE → 扫描结果处理/漫游
│   └── IP_EVENT_STA_GOT_IP → 启动 SNTP / 发布事件
├── esp_wifi_start()
└── wifi_autoconnect() → 连接上次保存的 SSID
```

### 4.3 配网流程（SoftAP + Captive Portal）

```
wifi_provision_start()
├── 设置 WiFi 为 AP_STA 模式
├── 配置 AP 网络（192.168.4.1）
├── 启动 DNS 劫持（所有域名解析到 192.168.4.1）
├── 启动 HTTP 配网服务
├── 等待用户通过浏览器访问 http://192.168.4.1
├── 接收 WiFi 配置（SSID + 密码）
├── 切换回 STA 模式
├── 连接指定 WiFi
├── 连接成功 → 保存凭证到 NVS
└── 连接失败 → 返回配网模式
```

---

## 五、核心模块详解

### 5.1 配置管理（app_cfg）

**设计原则**：
- 全局配置实例 `g_cfg` 使用互斥锁保护（`cfg_lock()/cfg_unlock()`）
- 配置变更通过事件总线发布（`EVENT_CFG_CHANGED`）
- NVS 持久化采用增量更新策略
- WiFi 凭证采用"两阶段提交"：先暂存，连接成功后再落地

**配置字段分类**：

| 分类 | 字段 |
|------|------|
| 基础 | tz_idx, brightness, dim_s, off_s, hour24, date_fmt |
| 时钟 | clock_x, clock_y, clock_size, clock_rgba, show_clock |
| 背景 | bg_mode, bg_url, bg_color, bg_refresh_s |
| 行情 | quotes_sym_l, quotes_sym_r, quotes_refresh_s |
| 音频 | audio_enable, audio_volume |
| WiFi | last_ssid, wifi_autoconnect |

### 5.2 数据源架构（data_source）

采用 **C 语言 VTable 模式** 实现多态：

```c
typedef struct {
    bool (*init)(DataSource *ds);
    bool (*connect)(DataSource *ds);
    void (*disconnect)(DataSource *ds);
    bool (*poll)(DataSource *ds);
    bool (*is_connected)(DataSource *ds);
    const NasData* (*get_data)(DataSource *ds);
    // ...
} DataSourceVTable;
```

**支持的 NAS 类型**：

| 类型 ID | 显示名称 | 端口 | 认证 |
|---------|---------|------|------|
| `synology` | Synology DSM | 5000 | HTTP Auth |
| `qnap` | QNAP QTS | 8080 | HTTP Auth |
| `truenas` | TrueNAS | 80 | HTTP Auth |
| `netdata` | Netdata | 19999 | None |
| `snmp` | SNMP | 161 | SNMP v2c |
| `linux_http` | Linux (HTTP) | 8099 | None |
| `mock` | Mock (测试) | - | None |

### 5.3 NAS 数据采集循环

```
nas_event_loop_start()
├── 创建互斥锁
├── 创建数据来源（根据配置或 mock）
├── 启动 FreeRTOS 任务 task_nas_data_loop
│   └── 循环接收事件
│       ├── EVENT_TRIGGER_HTTP_FETCH → data_source_poll() → 发布 EVENT_NAS_DATA_UPDATE
│       ├── EVENT_WIFI_CONNECTED → 启用数据采集
│       └── EVENT_WIFI_DISCONNECTED / EVENT_HTTP_STOP → 禁用数据采集
```

### 5.4 UI 架构

**屏幕导航**：

```
Boot 屏幕（启动时）
    ↓ 点击或超时
Overview 屏幕（主屏幕）
    ←→ 手势滑动
Settings 屏幕 ←→ Overview ←→ Storage 屏幕
```

**屏幕文件结构**（SquareLine 风格）：
- `ui_Screen_xxx.c/h`：屏幕定义和创建函数
- `ui_Screen_xxx_screen_init()`：初始化屏幕
- `ui_event_Screen_xxx_yyy()`：事件回调

**UI 事件处理**：[ui_events.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/ui/ui_events.c)
- 手势导航（左右滑动切换屏幕）
- 自动变暗/关闭定时器（dim_timer）
- NAS 数据更新 UI 刷新
- WiFi 状态显示

**UI 状态管理**：[ui_helpers.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/ui/ui_helpers.c)
- TileView 管理
- 活动时间戳记录
- 背光控制
- 菜单输入阻塞

### 5.5 背景图片获取（bg_fetcher）

**特点**：
- 独立 FreeRTOS 任务，优先级 4
- 仅支持原始 RGB565 格式（PNG/JPEG 解码器已禁用以节省内存）
- 通过事件总线通知 UI 刷新背景
- 支持刷新间隔配置（`bg_refresh_s`）

---

## 六、关键设计模式

### 6.1 事件总线模式

所有模块间通信通过 `event_bus_publish` / `event_bus_subscribe` 进行，解耦模块依赖。

### 6.2 VTable 模式

数据源使用 VTable 实现多态，新增 NAS 类型只需实现接口并注册。

### 6.3 两阶段提交（WiFi 凭证）

```
app_cfg_wifi_pending_set() → 暂存凭证
    ↓
wifi_connect() → 发起连接
    ↓
连接成功 → app_cfg_wifi_pending_commit() → 落地到 NVS
连接失败 → 不保存，避免错误密码污染 NVS
```

### 6.4 懒初始化（event_bus）

event_bus 在首次 `subscribe` 或 `publish` 时自动初始化，简化调用方代码。

---

## 七、硬件接口

### 7.1 I2C 设备

| 设备 | 地址 | 说明 |
|------|------|------|
| TCA9554 | 0x20 | IO 扩展器（电源控制） |
| PCF8563 | 0x51 | RTC 实时时钟 |
| BMA423 | 0x18 | IMU 加速度计 |
| CST920 | 0x15 | 触摸屏控制器 |

### 7.2 SPI 设备

| 设备 | 说明 |
|------|------|
| AXS15231B | LCD 面板驱动 |
| SD Card | 存储扩展 |

### 7.3 音频设备

| 设备 | 功能 |
|------|------|
| ES8311 | I2S DAC（播放） |
| ES7210 | I2S ADC（录音） |
| 配置 | TDM 模式，共享 BCLK/WS 时钟 |

---

## 八、开发与调试

### 8.1 编译烧录

```powershell
# 编译
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py build

# 烧录
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py -p COM5 flash

# 监控
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py -p COM5 monitor
```

### 8.2 日志级别

```c
esp_log_level_set("*", ESP_LOG_INFO);        // 全局默认 INFO
esp_log_level_set("skeleton", ESP_LOG_DEBUG); // 主模块 DEBUG
esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE); // LCD 详细
esp_log_level_set("lcd_panel.io.spi", ESP_LOG_VERBOSE);    // SPI 详细
```

### 8.3 CLI 命令

项目支持命令行接口（[cli.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/cli.c)），可通过串口交互。

### 8.4 WebUI 接口

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/wifi-scan` | GET | 扫描 WiFi 网络 |
| `/api/wifi-connect` | POST | 连接 WiFi |
| `/api/nas-settings` | GET/POST | NAS 配置 |

---

## 九、注意事项

### 9.1 内存限制
- DMA 缓冲区必须位于内部 RAM，且需 4/32 字节对齐
- PSRAM 用于 LVGL 帧缓冲区和背景图片
- PNG/JPEG 解码器已禁用，背景仅支持原始 RGB565 格式

### 9.2 线程安全
- `g_cfg` 访问需使用 `cfg_lock()/cfg_unlock()`
- 从非 LVGL 任务操作 UI 需使用 `lvgl_lock()/lvgl_unlock()`
- WiFi 状态变量 `g_wifi_connected` 仅在 IP_EVENT_STA_GOT_IP 事件中更新

### 9.3 配网超时
- SoftAP 配网模式无内置超时机制，需外部控制

### 9.4 时区设置
- 时区在 `system_time_init()` 中通过 `tz_apply_current()` 应用
- SNTP 同步成功后自动回写 RTC

---

## 十、版本历史

| 版本 | 变更 |
|------|------|
| v7 | 配置版本迁移，新增字段 |
| - | 删除屏幕旋转重建功能 |
| - | 清理 main.cpp 冗余代码，职责分离 |
| - | EVENT_CFG_CHANGED 事件处理迁移至 ui_events.c |

---

*文档生成时间：2026-07-08*