# ZotLab NAS Monitor - 项目 Wiki

## Overview

- **项目名称**: ZotLab NAS Monitor
- **项目描述**: ESP32-S3 平台的 NAS 系统监控设备，配备 3.49" 触摸屏（640x172），支持监控 Synology、QNAP、TrueNAS 等多种 NAS 设备的系统状态、磁盘、网络等信息。
- **硬件平台**: ESP32-S3 + 3.49" AXS15231B LCD + 8MB PSRAM + 16MB Flash
- **软件框架**: ESP-IDF v5.5.4 + LVGL 图形库
- **屏幕方向**: 横向显示（640x172），支持旋转

---

## 目录结构

### 顶层目录

| 目录 | 职责 | 说明 |
|---|---|---|
| `main/` | 主应用代码 | 核心业务逻辑、UI、网络、驱动等 |
| `components/` | ESP-IDF 组件 | 第三方库和自定义 BSP 组件 |
| `.trae/` | Trae 工具配置 | 项目规则、技能模板等 |
| `build/` | 构建输出 | CMake 构建产物（.gitignore） |

### main/ 目录结构

```
main/
├── config/           # 配置管理（NVS持久化）
│   ├── app_cfg.c/h   # 应用配置读写 API
│   ├── config.c/h    # 配置定义和初始化
│   └── app_info.h    # 应用版本和常量定义
├── data/             # 数据层（NAS监控数据）
│   ├── client/       # NAS客户端实现
│   │   ├── api_client.c/h     # 通用API客户端
│   │   ├── synology_client.c/h # Synology NAS客户端
│   │   ├── qnap_client.c/h     # QNAP NAS客户端
│   │   ├── truenas_client.c/h  # TrueNAS客户端
│   │   ├── netdata_client.c/h  # Netdata客户端
│   │   ├── snmp_client.c/h     # SNMP客户端
│   │   ├── serial_client.c/h   # 串口客户端
│   │   └── mock_client.c/h     # Mock客户端（测试用）
│   ├── data_source.c/h       # 数据源抽象层
│   ├── nas_data.h            # NAS数据结构体定义
│   ├── nas_data_json.c/h     # JSON解析
│   └── nas_event_loop.c/h    # NAS数据更新事件循环
├── drivers/          # 设备驱动
│   └── disp_driver.c/h       # LCD显示驱动 + LVGL集成
├── network/          # 网络模块
│   ├── sntp_manager.c/h      # SNTP时间同步
│   └── wifi_provision.*      # WiFi配网（已移除）
├── ui/               # 用户界面（LVGL）
│   ├── screens/      # 屏幕定义
│   │   ├── ui_Screen_Boot.c/h        # 启动屏幕
│   │   ├── ui_Screen_Overview.c/h    # 概览屏幕
│   │   ├── ui_Screen_Settings.c/h    # 设置屏幕
│   │   ├── ui_Screen_Storage.c/h     # 存储屏幕
│   │   ├── ui_Screen_WifiConfig.c/h  # WiFi配置屏幕
│   │   └── ui_Screen_Settings_*Tab.c/h # 设置标签页
│   ├── fonts/        # 字体资源（JBMono系列）
│   ├── images/       # 图片资源
│   ├── ui.c/h        # UI入口和初始化
│   ├── ui_events.c/h # 事件处理（手势、定时器等）
│   ├── ui_helpers.c/h # UI辅助函数
│   └── old/          # 旧版UI组件（已废弃）
├── utils/            # 工具模块
│   ├── event_bus.c/h      # 事件总线
│   ├── hw_init.c/h        # 硬件初始化入口
│   ├── i18n.c/h           # 国际化
│   ├── theme.c/h          # 主题管理
│   ├── bg_fetcher.c/h     # 背景图片获取
│   ├── cli.c/h            # 命令行接口
│   ├── http_timer.c/h     # HTTP定时任务
│   └── wifi_adapter.c/h   # WiFi适配层
├── CMakeLists.txt    # 构建配置
├── main.cpp          # 应用入口（C++）
├── user_config.h     # 用户自定义配置
└── wifi_secret.example.h # WiFi凭证模板
```

### components/ 目录结构

| 组件 | 说明 |
|---|---|
| `lvgl/` | LVGL 图形库（官方组件） |
| `SensorLib/` | 传感器库（Bosch、触摸等） |
| `codec_board/` | 编解码器板支持 |
| `i2c_bsp/` | I2C 总线 BSP |
| `i2c_equipment/` | I2C 设备抽象 |
| `lcd_bl_pwm_bsp/` | LCD背光 PWM BSP |
| `button_bsp/` | 按钮 BSP |
| `audio_min/` | 音频最小化驱动 |
| `adc_bsp/` | ADC BSP |

---

## 核心架构

### 启动流程

```
app_main()
    │
    ├─► log_init()              # 日志初始化
    ├─► event_bus_init()        # 事件总线初始化
    ├─► app_cfg_load()          # 配置加载（NVS）
    │
    ├─► hw_init()               # 硬件初始化
    │       ├─► I2C总线
    │       ├─► TCA9554 IO扩展器
    │       ├─► LCD背光PWM
    │       ├─► ADC/BUTTON/SDCARD/AUDIO初始化
    │       └─► LCD面板 + LVGL
    │
    ├─► network_init()          # 网络初始化
    │       ├─► esp_bus_init()
    │       ├─► wifi_cfg_init() # WiFi配置（SoftAP配网）
    │       │   ├─► provisioning_mode = WIFI_PROV_WHEN_UNPROVISIONED
    │       │   ├─► default_ap = { NAS-Monitor, 12345678 }
    │       │   └─► enable_ap = true
    │       └─► webui_start()   # WebUI启动（复用配网HTTP服务器）
    │
    ├─► nas_event_loop_start()  # NAS数据更新循环
    ├─► http_timer_init()       # HTTP定时任务
    ├─► http_timer_start()      # 启动HTTP定时器
    ├─► ui_init()               # UI初始化
    │       ├─► Boot屏幕
    │       ├─► 进度动画
    │       └─► 跳转WiFi配置屏幕
    ├─► cli_start()             # CLI启动
    └─► system_monitor_start()  # 系统监控启动
```

### 屏幕切换流程

```
Boot屏幕 → WiFi配置屏幕 → 主界面（TileView）
                              ├─► Clock页面（时钟）
                              ├─► Quotes页面（行情）
                              ├─► Settings页面（设置）
                              └─► Hello页面（欢迎）
```

---

## 核心模块详解

### 1. 配置管理 (app_cfg)

**职责**: 通过 NVS（Non-Volatile Storage）持久化存储用户配置

**NVS命名空间**:
- `cfg` - 应用配置
- `wifi` - WiFi凭证

**配置结构体**: [app_cfg_t](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/config/app_cfg.h#L89-L121)

**关键字段**:
| 字段 | 类型 | 说明 |
|---|---|---|
| `version` | uint8_t | 配置版本号 |
| `tz_idx` | uint16_t | 时区城市索引 |
| `brightness` | uint8_t | 背光亮度 (0-255) |
| `dim_s` | uint16_t | 自动变暗延迟（秒） |
| `off_s` | uint16_t | 自动关闭延迟（秒） |
| `last_ssid` | char[33] | 最后连接的WiFi SSID |
| `hour24` | uint8_t | 24小时制标志 |
| `date_fmt` | uint8_t | 日期格式 (0=YYYY.MM.DD, 1=DD.MM.YYYY, 2=MM.DD.YYYY) |
| `show_seconds` | uint8_t | 显示秒数标志 |
| `show_ms` | uint8_t | 显示毫秒标志 |
| `audio_enable` | uint8_t | 音频使能标志 |
| `audio_volume` | uint8_t | 音频音量 (0-100) |
| `theme` | uint8_t | 主题索引 |
| `show_fps` | uint8_t | 显示FPS标志 |
| `wifi_autoconnect` | uint8_t | WiFi自动连接标志 |
| `lang` | uint8_t | 语言索引 |
| `clock_x/y` | int16_t | 时钟位置偏移 |
| `clock_size` | uint8_t | 时钟字号大小 (0-3) |
| `clock_rgba` | uint32_t | 时钟文字颜色 (RGBA) |
| `show_clock` | uint8_t | 显示时钟标志 |
| `clock_text` | char[33] | 自定义时钟文本 |
| `bg_mode` | uint8_t | 背景模式 (0=深色, 1=浅色, 2=图片, 3=纯色) |
| `bg_refresh_s` | uint16_t | 背景刷新间隔（秒） |
| `bg_url` | char[128] | 背景图片URL |
| `bg_color` | uint32_t | 背景纯色 (RGBA) |
| `quotes_sym_l/r` | char[16] | 左右行情符号 |
| `quotes_refresh_s` | uint16_t | 行情刷新间隔（秒） |
| `quotes_up_rgba` | uint32_t | 行情上涨颜色 (RGBA) |
| `quotes_down_rgba` | uint32_t | 行情下跌颜色 (RGBA) |

**配置版本**: `CFG_VERSION = 7`，用于配置迁移

### 2. 事件总线 (event_bus)

**职责**: 全局事件分发机制，实现模块间解耦

**事件类型**: [event_id_t](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/utils/event_bus.h#L14-L49)

**核心事件**:
| 事件 | 说明 |
|---|---|
| `EVENT_ROTATION_CHANGED` | 屏幕旋转变更 |
| `EVENT_WIFI_CONNECTED` | WiFi连接成功 |
| `EVENT_WIFI_DISCONNECTED` | WiFi断开 |
| `EVENT_WIFI_SCAN_DONE` | WiFi扫描完成 |
| `EVENT_WIFI_SCAN_STARTED` | WiFi扫描开始 |
| `EVENT_CFG_CHANGED` | 配置变更 |
| `EVENT_CLOCK_LAYOUT_CHANGED` | 时钟布局变更 |
| `EVENT_CLOCK_BG_CHANGED` | 时钟背景变更 |
| `EVENT_CLOCK_TIME_FORMAT_CHANGED` | 时钟时间格式变更 |
| `EVENT_QUOTES_CHANGED` | 行情数据变更 |
| `EVENT_SHOW_FPS_CHANGED` | FPS显示开关变更 |
| `EVENT_STORAGE_CHANGED` | 存储状态变更 |
| `EVENT_AUDIO_PLAY_START` | 音频播放开始 |
| `EVENT_AUDIO_PLAY_STOP` | 音频播放停止 |
| `EVENT_AUDIO_RECORD_START` | 录音开始 |
| `EVENT_AUDIO_RECORD_STOP` | 录音停止 |
| `EVENT_AUDIO_VOLUME_CHANGED` | 音量变更 |
| `EVENT_USER_ACTIVITY` | 用户活动 |
| `EVENT_BACKLIGHT_CHANGED` | 背光亮度变更 |
| `EVENT_TILE_CHANGED` | Tile页面切换 |
| `EVENT_TICK_1HZ` | 1Hz心跳 |
| `EVENT_TICK_10HZ` | 10Hz心跳 |
| `EVENT_NAS_DATA_UPDATE` | NAS数据更新 |
| `EVENT_TRIGGER_HTTP_FETCH` | 触发HTTP获取 |
| `EVENT_HTTP_STOP` | HTTP停止 |
| `EVENT_WIFI_PROVISION_START` | WiFi配网开始 |
| `EVENT_WIFI_PROVISION_STOP` | WiFi配网停止 |
| `EVENT_WIFI_PROVISION_CONFIG_RECEIVED` | WiFi配网配置接收 |

**API**:
```c
void event_bus_init(void);
void event_bus_publish(event_id_t id, void *data, size_t len);
void event_bus_subscribe(event_id_t id, event_handler_t handler, void *user_data);
void event_bus_unsubscribe(event_id_t id, event_handler_t handler);
```

### 3. 显示驱动 (disp_driver)

**职责**: LCD面板驱动和LVGL集成

**硬件规格**:
- LCD: AXS15231B (640x172)
- PSRAM: 8MB (用于DMA缓冲区)
- SPI Flash: 16MB

**关键全局变量**:
| 变量 | 说明 |
|---|---|
| `g_fps_frame_count` | FPS帧计数器 |
| `g_fps_label` | FPS显示标签 |
| `g_rot_state` | 旋转状态 (0-3) |
| `g_canvas_w/h` | 画布尺寸 |

**LVGL同步**: 使用 `lvgl_lock()` / `lvgl_unlock()` 保护LVGL操作

### 4. NAS数据层 (nas_data)

**职责**: 定义NAS监控数据结构

**支持的NAS类型**: [NasType](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/nas_data.h#L11-L24)

| 类型 | 说明 |
|---|---|
| `NAS_SYNOLOGY` | Synology NAS |
| `NAS_QNAP` | QNAP NAS |
| `NAS_TRUENAS` | TrueNAS |
| `NAS_FNOS` | FreeNAS/FreeBSD |
| `NAS_UNRAID` | Unraid |
| `NET_LINUX_HTTP` | Linux HTTP接口 |
| `NET_LINUX_SERIAL` | Linux串口 |
| `NET_WINDOWS` | Windows系统 |
| `NET_NETDATA` | Netdata监控 |
| `NET_SNMP` | SNMP协议 |
| `NAS_MOCK` | Mock数据（测试） |

**核心数据结构**: [NasData](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/nas_data.h#L107-L123)

包含:
- `NasSystemInfo` - 系统信息（CPU、内存、温度）
- `NasDiskInfo` - 磁盘信息（温度、健康状态、使用率）
- `NasVolumeInfo` - 卷信息（容量、RAID类型）
- `NasServiceInfo` - 服务状态
- `NasNetworkInfo` - 网络流量
- `FanStatus` - 风扇状态

### 5. UI系统

**屏幕管理**:
- `ui_Screen_Boot` - 启动屏幕（进度条动画）
- `ui_Screen_WifiConfig` - WiFi配置屏幕（独立页面）
- `ui_Screen_Overview` - 概览屏幕
- `ui_Screen_Settings` - 设置屏幕（多标签页）
- `ui_Screen_Storage` - 存储屏幕

**设置标签页**:
| 标签 | 文件 |
|---|---|
| WiFi配置 | `ui_Screen_Settings_WifiTab.c` |
| NAS配置 | `ui_Screen_Settings_NasTab.c` |
| 屏幕设置 | `ui_Screen_Settings_ScreenTab.c` |
| 站点配置 | `ui_Screen_Settings_StationTab.c` |
| 音乐设置 | `ui_Screen_Settings_MusicTab.c` |
| 区域设置 | `ui_Screen_Settings_RegionTab.c` |
| 指南 | `ui_Screen_Settings_GuideTab.c` |

**字体资源**:
| 字体 | 大小 | 用途 |
|---|---|---|
| `font_jbmono_24` | 24px | 常规文本 |
| `font_jbmono_48` | 48px | 大文本 |
| `font_jbmono_64` | 64px | 时钟 |
| `font_jbmono_96` | 96px | 大字时钟 |
| `font_cjk_14` | 14px | CJK文本 |

### 6. 网络模块

**WiFi管理**: 使用 `esp_wifi_config` 组件进行WiFi配置和配网

**配网方式**: SoftAP模式（SSID: NAS-Monitor, 密码: 12345678）

**SNTP管理**: `sntp_manager` 同步网络时间

**HTTP服务**: `webui_start()` 启动Web管理界面

**WiFi适配层**: [wifi_adapter](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/utils/wifi_adapter.h) 提供统一的WiFi操作接口

| 函数 | 说明 |
|---|---|
| `wifi_start_provisioning()` | 启动SoftAP配网 |
| `wifi_connect(ssid, password)` | 连接WiFi |
| `wifi_disconnect()` | 断开连接 |
| `wifi_start_scan()` | 扫描WiFi |
| `wifi_is_connected()` | 检查连接状态 |
| `wifi_has_credentials()` | 检查是否有存储的WiFi凭证 |

### 7. 数据源抽象层 (data_source)

**职责**: 使用抽象工厂模式管理各类NAS客户端，实现多数据源归一化

**DataSourceVTable接口**:
| 方法 | 说明 |
|---|---|
| `init()` | 初始化客户端 |
| `connect()` | 建立连接 |
| `disconnect()` | 断开连接 |
| `poll()` | 轮询数据 |
| `is_connected()` | 检查连接状态 |
| `get_data()` | 获取数据 |
| `get_type_name()` | 获取类型名称 |
| `destroy()` | 销毁客户端 |

**核心API**:
| 函数 | 说明 |
|---|---|
| `data_source_create(type_id)` | 创建数据源实例 |
| `data_source_init(type_id)` | 初始化数据源 |
| `data_source_connect()` | 连接数据源 |
| `data_source_disconnect()` | 断开数据源连接 |
| `data_source_poll()` | 轮询获取数据 |
| `data_source_switch(type_id)` | 切换数据源类型 |

---

## 关键技术概念

### 1. LVGL TileView

主界面使用 LVGL TileView 组件实现横向滑动页面切换：
- 4个Tile: Clock → Quotes → Settings → Hello
- 手势滑动切换
- 底部小圆点指示当前位置

### 2. 事件驱动架构

系统采用事件总线驱动：
- 模块通过 `event_bus_publish()` 发布事件
- 模块通过 `event_bus_subscribe()` 订阅感兴趣的事件
- 事件队列长度: `EVENT_QUEUE_LEN = 16`

### 3. WiFi配网流程

```
用户点击"AP Setup"按钮
    │
    ├─► 启动SoftAP（SSID: NAS-Monitor）
    ├─► 启动DNS服务器（重定向所有请求到设备IP）
    ├─► 启动HTTP服务器（提供配网页面）
    │
用户连接AP并打开浏览器
    │
    ├─► DNS重定向到192.168.4.1
    ├─► 显示配网页面
    ├─► 用户输入WiFi SSID和密码
    ├─► POST /config 提交配置
    │
设备接收配置
    │
    ├─► 切换到STA模式
    ├─► 连接目标WiFi
    ├─► 连接成功后发布 EVENT_WIFI_CONNECTED
    └─► UI自动跳转到主界面
```

### 4. 配置脏字段机制

`app_cfg` 实现了脏字段追踪：
- 修改配置时标记为脏
- `app_cfg_save()` 只写入脏字段（增量保存）
- `app_cfg_flush()` 强制同步所有脏字段

---

## 项目常量

### 屏幕尺寸

```c
#define TFT_HEIGHT  172
#define TFT_WIDTH   640
#define STATUS_BAR_H 24
#define CONTENT_H    (TFT_HEIGHT - STATUS_BAR_H - PAGE_DOT_H)
```

### 数据限制

```c
#define MAX_DISKS      16    // 最大磁盘数
#define MAX_VOLUMES    8     // 最大卷数
#define MAX_SERVICES   16    // 最大服务数
#define MAX_CPU_CORES  8     // 最大CPU核心数
#define NET_HISTORY_POINTS 30 // 网络历史点数
```

### 应用信息

```c
#define APP_NAME      "ZotLab NAS Monitor"
#define APP_VERSION   "1.1.0"
#define NAS_LOGO      "ZotLab"
#define NAS_TYPE      "Z6"
```

---

## 构建系统

### CMakeLists.txt

主构建文件: [CMakeLists.txt](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/CMakeLists.txt)

**源文件分类**:
1. 核心: `main.cpp`, `ui/ui.c`, `ui/ui_events.c`
2. UI组件: `ui/screens/*`, `ui/fonts/*`, `ui/images/*`
3. 网络: `network/*`
4. 数据: `data/*`, `data/client/*`
5. 工具: `utils/*`
6. 驱动: `drivers/*`

**包含目录**:
```
"."          # 根目录
"ui"         # UI目录
"drivers"    # 驱动目录
"network"    # 网络目录
"config"     # 配置目录
"utils"      # 工具目录
"data"       # 数据目录
```

### 编译定义

```
BUILD_EPOCH_UTC=<时间戳>
```

---

## 调试工具

### CLI命令行

通过串口访问，支持命令:
- WiFi相关命令
- 系统信息查询
- 配置读写

### ESP-IDF Monitor

```powershell
idf.py monitor -p COM5 -b 115200
```

### 日志级别

```c
esp_log_level_set("*", ESP_LOG_INFO);
esp_log_level_set("skeleton", ESP_LOG_DEBUG);
esp_log_level_set("lcd_panel.axs15231b", ESP_LOG_VERBOSE);
```

---

## 代码规范

### 文件命名

- 源文件: `snake_case.c/h`
- 屏幕文件: `ui_Screen_<Name>.c/h`
- 标签页文件: `ui_Screen_Settings_<TabName>Tab.c/h`

### 变量命名

- 全局变量: `g_` 前缀（如 `g_cfg`, `g_fps_label`）
- 静态变量: `s_` 前缀（如 `s_wifi_list`, `s_style_bg`）
- 局部变量: 无前缀

### 函数命名

- 公开函数: `module_action()`（如 `app_cfg_load()`）
- 静态函数: `s_action()`（如 `s_init_styles()`）
- 回调函数: `obj_event_cb()`（如 `ui_event_wifi_ap()`）

### UI文件五段式写法

每个 `ui_Screen_<Name>.c` 文件按以下五段顺序组织：

| 段 | 公开性 | 职责 |
|---|---|---|
| 1 声明区 | 私有 | 类型/对象/宏声明 |
| 2 构建区 | 私有 static | 纯创建/样式，禁止业务 |
| 3 逻辑区 | 私有 static | 回调/定时器/业务，禁止创建控件 |
| 4 init | 公开 | 装配屏幕 |
| 5 destroy | 公开 | 拆卸屏幕 |

---

## 扩展指南

### 添加新的NAS客户端

1. 在 `main/data/client/` 目录创建 `xxx_client.c/h`
2. 实现 `DataSourceVTable` 接口（`init`, `connect`, `disconnect`, `poll`, `is_connected`, `get_data`, `get_type_name`, `destroy`）
3. 在 `data_source.c` 的工厂函数中注册客户端创建逻辑
4. 在 `nas_data.h` 的 `NasType` 枚举中添加新类型
5. 在 `data_source.c` 的 `NAS_TYPES` 数组中添加类型定义

### 添加新的设置标签页

1. 在 `main/ui/screens/` 创建 `ui_Screen_Settings_<TabName>Tab.c/h`
2. 实现 `ui_Screen_Settings_<TabName>Tab_init()` 函数
3. 在 `ui_Screen_Settings.c` 中调用初始化函数

### 添加新的UI屏幕

1. 在 `main/ui/screens/` 创建 `ui_Screen_<Name>.c/h`
2. 按照五段式写法实现
3. 在 `CMakeLists.txt` 中添加源文件
4. 在 `ui.c` 或其他屏幕中添加跳转逻辑

---

## 常见问题

### WiFi配网失败

**可能原因**:
1. AP模式未正确启动
2. DNS服务器未启动
3. HTTP服务器端口冲突

**排查**:
- 查看串口日志中的 `wifi_provision` 相关输出
- 确认WiFi模式切换时序正确

### 屏幕显示异常

**可能原因**:
1. LVGL未正确初始化
2. DMA缓冲区不足
3. 屏幕旋转状态错误

**排查**:
- 检查 `disp_driver_init()` 返回值
- 查看FPS显示确认渲染正常

### NAS数据不更新

**可能原因**:
1. 网络连接问题
2. 客户端认证失败
3. HTTP请求超时

**排查**:
- 查看 `nas_event_loop` 日志
- 检查网络配置是否正确

---

## 版本历史

| 版本 | 说明 |
|---|---|
| 1.0.0 | 初始版本 |
| 1.1.0 | 添加WiFi配网、独立WiFi配置屏幕 |
