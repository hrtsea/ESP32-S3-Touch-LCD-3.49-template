# ESP32-S3-Touch-LCD 项目重构建议

> 梳理时间：2026-07-05
> 分支：重构前
> 代码基线：c77133e

---

## 一、当前代码结构总览

### 1.1 目录结构

```
ESP32-S3-Touch-LCD-3.49-template/
├── main/                          # 应用主程序（约 8,000 行业务代码）
│   ├── main.cpp                   # 程序入口（137 行，已较精简）
│   ├── CMakeLists.txt
│   ├── user_config.h              # 用户硬件配置
│   ├── config/                    # 配置管理
│   │   ├── app_cfg.c/h            # NVS 配置存储 + 回调注册
│   │   └── ...
│   ├── drivers/                   # 显示驱动
│   │   └── disp_driver.c/h        # LCD + LVGL + 旋转 + FPS
│   ├── network/                   # 网络管理
│   │   ├── wifi_manager.c/h       # WiFi 连接/扫描
│   │   └── sntp_manager.c/h       # 网络授时
│   ├── ui/                        # UI 模块（7 个页面）
│   │   ├── ui_main.c/h            # TileView 主框架 + 背光管理
│   │   ├── ui_hello.c/h           # Hello 演示页
│   │   ├── ui_clock.c/h           # 时钟 + 太阳轨迹图
│   │   ├── ui_quotes.c/h          # 行情卡片
│   │   ├── ui_radio.c/h           # 网络收音机
│   │   ├── ui_recorder.c/h        # 录音列表
│   │   ├── ui_audio_test.c/h      # 音频测试
│   │   └── ui_settings.c/h        # 设置页（最大，1090 行）
│   ├── utils/                     # 工具模块
│   │   ├── hw_init.c/h            # 硬件初始化汇总
│   │   ├── cli.c/h                # 串口命令行
│   │   ├── i18n.c/h               # 国际化
│   │   ├── theme.c/h              # 主题
│   │   ├── bg_fetcher.c/h         # 背景图下载
│   │   ├── system_monitor.c/h     # 系统监控
│   │   ├── landmask.h             # 太阳轨迹掩膜数据
│   │   ├── tz_cities.h            # 时区城市数据
│   │   └── i18n_strings.h         # 多语言字符串
│   └── fonts/                     # 字体资源
└── components/                    # 本地组件（15 个）
    ├── SensorLib/                 # 传感器驱动库（第三方，大量未用）
    ├── lvgl/                      # LVGL 图形库（第三方）
    ├── codec_board/               # 编解码板驱动（含 TCA9554 + LCD init）
    ├── webui/                     # Web 配置界面（964 行）
    ├── radio/                     # 网络收音机（453 行）
    ├── recorder/                  # 录音模块（411 行）
    ├── audio_min/                 # 最小音频播放（185 行）
    ├── audio_bsp/                 # 旧版音频 BSP（未使用，159 行）
    ├── i2c_bsp/                   # I2C 总线封装（103 行）
    ├── i2c_equipment/             # I2C 设备扫描（241 行）
    ├── button_bsp/                # 按键驱动（160 行）
    ├── adc_bsp/                   # ADC 驱动（75 行）
    ├── lcd_bl_pwm_bsp/            # 背光 PWM（44 行）
    └── sdcard_bsp/                # SD 卡驱动（140 行）
```

### 1.2 代码规模统计（业务代码，不含第三方库/字体）

| 模块 | 行数 | 占比 |
|------|------|------|
| ui_settings.c | 1,090 | 17.3% |
| webui.c | 964 | 15.3% |
| app_cfg.c | 680 | 10.8% |
| codec_init.c | 680 | 10.8% |
| ui_main.c | 491 | 7.8% |
| radio.c | 453 | 7.2% |
| ui_quotes.c | 429 | 6.8% |
| recorder.c | 411 | 6.5% |
| disp_driver.c | 408 | 6.5% |
| ui_clock.c | 402 | 6.4% |
| 其余 30+ 个文件 | ~2,700 | 42.6% |
| **合计** | **~6,300** | **100%** |

---

## 二、当前架构存在的主要问题

### 2.1 全局变量泛滥（最严重）

**问题描述**：大量模块将内部状态以 `g_` 前缀的全局变量暴露在头文件中，破坏了封装性，模块间可以随意读写彼此的状态。

**具体表现**：
- `disp_driver.h` 暴露 6 个全局变量：`g_fps_frame_count`, `g_fps_label`, `g_rot_state`, `g_canvas_w`, `g_canvas_h`
- `wifi_manager.h` 暴露 9 个全局变量：`g_wifi_scan[]`, `g_wifi_connected`, `g_wifi_curr_ssid`, `g_wifi_last_reason`, `g_wifi_last_rssi`, `g_wifi_connect_started_ms`, `g_wifi_scanning`, `g_wifi_scan_n`
- `ui_clock.h` 暴露 13 个全局变量（全部 LVGL widget 指针和 timer）
- `ui_quotes.h` 暴露 13 个全局变量
- `ui_recorder.h` 暴露 9 个全局变量
- `ui_common.h` 暴露 `g_tileview`, `g_status_text`, `g_last_scroll_ms`, `g_menu_input_block_until_ms`
- `ui_main.h` 暴露 `g_status_timer`, `g_last_activity_ms`, `g_dim_state`
- `app_cfg.h` 暴露 `g_cfg` 全局配置结构体
- `main.cpp` 中用 `extern` 声明了大量跨模块变量和函数

**危害**：
1. 模块边界模糊，任何模块都可以修改其他模块的内部状态
2. 难以追踪状态变更的来源，调试困难
3. 无法进行单元测试（依赖全局状态）
4. 多任务环境下存在竞态条件风险

### 2.2 模块职责边界模糊

**问题描述**：部分模块承担了过多职责，违反单一职责原则。

| 模块 | 当前职责 | 问题 |
|------|---------|------|
| `ui_settings.c` | WiFi 配置 + 存储管理 + 时钟设置 + 背景设置 + 行情设置 + 语言 + 音量 + ... | 1,090 行，混合了 UI 渲染、业务逻辑、NVS 操作、网络操作 |
| `ui_main.c` | TileView 管理 + 状态栏 + 背光控制 + 自动调光 + 用户活动检测 | 背光管理逻辑放在 UI 层不合理 |
| `app_cfg.c` | NVS 读写 + 配置校验 + 回调分发 + 业务逻辑（时钟背景加载等） | 配置层混入了业务逻辑（`app_cfg_clock_bg_reload`, `app_cfg_bg_fetch_now`） |
| `webui.c` | HTTP 服务器 + 配置接口 + 音频控制 + 文件管理 | 964 行，与 `app_cfg` 和 `radio`/`recorder` 高度耦合 |
| `hw_init.c` | 初始化所有硬件外设 | 只是一个顺序调用的胶水层，每个外设各自有 init，价值有限 |

### 2.3 循环依赖和隐式依赖

**问题描述**：模块间存在循环依赖和通过全局变量建立的隐式依赖。

**已知依赖关系**：
```
app_cfg.c → disp_driver (g_canvas_w/h) → ui_main (g_tileview)
ui_main.c → app_cfg (g_cfg) → ui_clock (clock_apply_layout)
ui_settings.c → ui_clock → ui_main → app_cfg → ...
webui.c → radio → recorder → radio (循环依赖！recorder REQUIRES radio)
```

**recorder/radio 循环依赖**：
- `recorder/CMakeLists.txt` 中 `REQUIRES radio`
- `radio.c` 又包含 `recorder` 的概念（录音增益、codec 配置）
- 实际上两个模块共享同一个 I2S 总线和 codec 硬件，但被拆成了两个组件

### 2.4 组件层次混乱（components vs main）

**问题描述**：哪些代码放 `components/`、哪些放 `main/` 没有清晰的标准。

| 组件 | 位置 | 分析 |
|------|------|------|
| radio | components/ | 业务逻辑（网络音频播放），不是可复用驱动 |
| recorder | components/ | 同上，业务逻辑 |
| webui | components/ | 业务逻辑（Web 配置界面），与 app_cfg 强耦合 |
| audio_min | components/ | 功能类（提示音播放） |
| audio_bsp | components/ | **未使用**，应删除 |
| i2c_equipment | components/ | 调试工具类，非核心功能 |
| codec_board | components/ | 驱动类，但包含了 LCD init（职责越界） |
| SensorLib | components/ | **大量未使用**，仅用了极少部分 |

`components/` 应该存放可复用的、与业务无关的驱动和库，但目前混入了大量业务组件。

### 2.5 事件/回调机制不统一

**问题描述**：模块间通信方式五花八门，没有统一的事件机制。

当前使用的通信方式：
1. **全局变量直接读写**（最常见，如 `g_wifi_connected`）
2. **app_cfg 的回调注册**（`app_cfg_register_callbacks`，仅 6 个回调）
3. **FreeRTOS 任务 + 全局标志位**（ui_audio_test 的 `g_state`, `g_stop_req`）
4. **LVGL 事件回调**（UI 内部）
5. **直接函数调用**（紧耦合）

没有统一的事件总线（event bus）或消息队列机制。

### 2.6 音频子系统架构混乱

**问题描述**：音频相关代码分散在多个组件中，职责重叠。

音频相关组件：
- `audio_bsp/` — 旧版 BSP（**未使用**）
- `audio_min/` — 最小音频播放（提示音），依赖 `radio`
- `radio/` — 网络收音机，负责 I2S + ES8311 + ES7210 初始化
- `recorder/` — 录音模块，依赖 `radio`（获取 codec 句柄）
- `codec_board/` — 编解码板驱动（codec_init）
- `ui_audio_test.c` — 音频测试页面，直接操作 codec 设备

**问题**：
1. `audio_bsp` 完全未使用，应删除
2. `recorder` 依赖 `radio` 来获取 codec 句柄，违反单向依赖原则
3. 真正的硬件初始化（codec + I2S）散落在 `radio.c` 和 `codec_board/` 中
4. `audio_min` 依赖 `radio` 只是为了拿播放设备句柄

### 2.7 UI 模块间的公共状态管理混乱

**问题描述**：多个 UI 页面共享的状态（如 `g_last_scroll_ms`, `g_menu_input_block_until_ms`）被放在 `ui_common.h` 中，变成了全局共享状态。

具体来说：
- `g_last_scroll_ms` 和 `g_menu_input_block_until_ms` 在 `ui_settings.c` 中定义，但被声明在 `ui_common.h` 中
- `g_tileview` 是整个 UI 的根容器，被多个模块引用
- `g_status_text` 是状态栏文本，也被多个模块引用

这些本质上是 UI 层的全局状态，缺乏统一管理。

### 2.8 配置层（app_cfg）过于臃肿

**问题描述**：`app_cfg` 模块承担了太多职责，而且接口膨胀。

- `app_cfg.h` 中有 **60+ 个 API 函数**（getter/setter 各一套）
- 配置结构体 `app_cfg_t` 有 **30+ 个字段**
- 包含了业务逻辑（如 `app_cfg_clock_bg_reload`, `app_cfg_bg_fetch_now`）
- 回调结构体 `app_cfg_callbacks_t` 只有 6 个回调，覆盖不全

理想情况下，配置层应该只负责：加载 / 保存 / 提供访问接口，不包含业务逻辑。

---

## 三、重构目标架构

### 3.1 整体分层架构

```
┌───────────────────────────────────────────────────────────┐
│                     Application Layer                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  WebUI   │  │  CLI     │  │  UI App  │  │  Audio   │  │
│  │  (HTTP)  │  │  (UART)  │  │  (LVGL)  │  │  Service │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │
├───────┼─────────────┼─────────────┼─────────────┼────────┤
│       └─────────────┴────────┬────┴─────────────┘        │
│                     Service Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │ Config   │  │ Network  │  │ Audio    │  │ UI State │  │
│  │ Manager  │  │ Manager  │  │ Manager  │  │ Manager  │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │
├───────┼─────────────┼─────────────┼─────────────┼────────┤
│       └─────────────┴────────┬────┴─────────────┘        │
│                     Driver Layer                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  Display │  │  Audio   │  │  Periph  │  │ Storage  │  │
│  │  Driver  │  │  HAL     │  │  (I2C/GPIO/ADC) │ (SD/NVS) │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │
└───────────────────────────────────────────────────────────┘
```

### 3.2 核心设计原则

1. **依赖单向性**：上层依赖下层，下层不感知上层
2. **事件驱动**：模块间通过事件总线通信，不直接调用
3. **封装性**：模块内部状态不外露，通过 API 访问
4. **单一职责**：每个模块只做一件事
5. **可测试性**：模块可独立编译和测试

---

## 四、具体重构建议

### 建议 1：引入事件总线（Event Bus）

**优先级**：★★★★★（最高）

**方案**：基于 FreeRTOS 的事件组（Event Group）或自定义消息队列，实现模块间的解耦通信。

```c
// event_bus.h
typedef enum {
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_CFG_CHANGED,
    EVENT_AUDIO_PLAY_START,
    EVENT_AUDIO_PLAY_STOP,
    EVENT_ROTATION_CHANGED,
    EVENT_USER_ACTIVITY,
    // ...
} event_id_t;

typedef struct {
    event_id_t id;
    void      *data;
    size_t     data_len;
} event_t;

void event_bus_init(void);
void event_bus_publish(event_id_t id, void *data, size_t len);
void event_bus_subscribe(event_id_t id, void (*callback)(const event_t *evt));
```

**收益**：
- 消除模块间的直接函数调用依赖
- 新增功能只需订阅/发布事件，不修改现有代码
- 便于调试（可统一打印事件流）

**工作量**：约 200 行代码 + 各模块改造

---

### 建议 2：封装全局变量，提供访问 API

**优先级**：★★★★★

**方案**：将所有 `g_` 前缀的全局变量改为 `static`，提供 getter/setter API，关键操作加锁保护。

**示例**（disp_driver）：

```c
// disp_driver.h（修改后）
int  disp_driver_get_rot_state(void);
void disp_driver_set_rot_state(int state);
int  disp_driver_get_canvas_w(void);
int  disp_driver_get_canvas_h(void);
void disp_driver_get_canvas_size(int *w, int *h);
```

```c
// disp_driver.c（修改后）
static int s_rot_state = 1;
static int s_canvas_w = UI_CANVAS_W;
static int s_canvas_h = UI_CANVAS_H;

int disp_driver_get_rot_state(void) { return s_rot_state; }
int disp_driver_get_canvas_w(void)  { return s_canvas_w; }
// ...
```

**需要改造的模块**：
| 模块 | 全局变量数 | 预计工作量 |
|------|----------|----------|
| wifi_manager | 9 | 中等 |
| ui_clock | 13 | 大（widget 指针改为内部静态） |
| ui_quotes | 13 | 大 |
| ui_recorder | 9 | 中 |
| disp_driver | 6 | 小 |
| ui_main（背光状态） | 3 | 小 |

**收益**：
- 模块封装性大幅提升
- 可以在 setter 中加入边界检查和事件通知
- 多任务安全（可在 API 内部加锁）

---

### 建议 3：拆分 ui_settings.c

**优先级**：★★★★☆

**方案**：将 1,090 行的 `ui_settings.c` 按功能拆分为多个子页面，每个子页面一个文件。

```
ui/settings/
├── ui_settings.c/h        # 设置页主框架（页面导航 + 滚动管理）
├── ui_settings_wifi.c     # WiFi 设置（扫描 + 连接 + 键盘）
├── ui_settings_storage.c  # 存储管理（SD 卡信息 + 格式化）
├── ui_settings_display.c  # 显示设置（亮度 + 主题 + 旋转 + 时钟）
├── ui_settings_audio.c    # 音频设置（音量 + 使能）
├── ui_settings_system.c   # 系统设置（语言 + 时区 + 重启）
└── ui_settings_quotes.c   # 行情设置（符号 + 刷新间隔 + 颜色）
```

**收益**：
- 单个文件从 1,090 行降到 200-300 行
- 每个设置项独立，便于维护和测试
- 新增设置项只需新增文件，不触碰现有代码

**工作量**：较大（拆分 + 抽取公共组件）

---

### 建议 4：重构音频子系统

**优先级**：★★★★☆

**方案**：建立统一的音频 HAL 层，将 codec 管理从 radio 中剥离出来。

```
components/audio_hal/          # 新增：音频硬件抽象层
├── audio_hal.c/h
├── audio_hal_codec.c/h        # ES8311 + ES7210 管理
└── audio_hal_i2s.c/h          # I2S TDM 配置

components/audio_player/       # 从 radio 中剥离：播放引擎
├── audio_player.c/h           # 通用播放接口
└── audio_player_net.c         # 网络流播放

components/audio_recorder/     # 保持独立，但依赖 audio_hal 而非 radio
└── audio_recorder.c/h
```

**核心变化**：
1. `audio_hal` 管理 codec 初始化、采样率切换、音量/增益
2. `radio` 改名为 `audio_player_net`，只负责网络流解码
3. `recorder` 依赖 `audio_hal`，不再依赖 `radio`
4. `audio_min` 也依赖 `audio_hal`，不再绕路通过 radio
5. 删除 `audio_bsp`（已废弃）

**解决的问题**：
- 消除 recorder → radio 的反向依赖
- 采样率切换逻辑集中管理（TDM 同步）
- 硬件初始化不再散落在各处

---

### 建议 5：精简 app_cfg，拆分配置域

**优先级**：★★★☆☆

**方案**：将庞大的 `app_cfg_t` 按领域拆分为多个配置域，每个域有独立的加载/保存和回调。

```c
typedef struct {
    uint8_t  version;
    // 显示域
    uint8_t  brightness;
    uint8_t  theme;
    uint8_t  show_fps;
    // 时钟域
    uint8_t  hour24;
    uint8_t  date_fmt;
    uint16_t clock_size;
    // ...
} app_cfg_t;
```

同时：
1. 从 `app_cfg` 中移除业务逻辑（`clock_bg_reload`, `bg_fetch_now` 等），改由事件驱动
2. 提供通用的 `app_cfg_notify_changed(KEY)` 机制，替代硬编码的 6 个回调
3. getter/setter 可以用宏生成，减少代码量

---

### 建议 6：建立 UI State 管理器

**优先级**：★★★☆☆

**方案**：将 UI 层的全局状态（`g_tileview`, `g_status_text`, `g_last_scroll_ms`, `g_menu_input_block_until_ms` 等）集中到一个 `ui_state` 模块中管理。

```
ui/ui_state.c/h
├── ui_state_get_tileview()
├── ui_state_set_status_text()
├── ui_state_get_last_scroll_ms()
├── ui_state_set_menu_block()
├── ui_state_lock() / ui_state_unlock()   // LVGL 互斥
└── ...
```

**收益**：
- UI 全局状态有明确的归属
- 可以在状态变更时自动触发 UI 更新
- 统一管理 LVGL 锁

---

### 建议 7：清理 components 目录

**优先级**：★★★☆☆

**方案**：

| 组件 | 建议操作 | 理由 |
|------|---------|------|
| `audio_bsp` | **删除** | 未使用，已被 radio/audio_min 取代 |
| `SensorLib` | **裁剪** | 保留用到的部分（如果有用到），否则删除 |
| `i2c_equipment` | 移到 `tools/` 或 `debug/` | 调试工具，非核心组件 |
| `radio` | 重构为 `audio_player_net`（见建议 4） | 命名不准确，职责越界 |
| `webui` | 移到 `main/app/` | 业务逻辑，不属于可复用组件 |
| `recorder` | 重构（见建议 4） | 依赖方向错误 |

---

### 建议 8：进一步精简 main.cpp

**优先级**：★★☆☆☆

当前 `main.cpp` 已经只有 137 行，比较精简。但仍可以进一步向"纯组装"方向发展：

```cpp
extern "C" void app_main(void)
{
    // 1. 基础驱动初始化
    board_init();        // 所有硬件外设初始化（从 hw_init.c 抽象而来）

    // 2. 服务层初始化
    service_mgr_init();  // 配置 + 网络 + 音频 + 事件总线

    // 3. 应用层初始化
    app_ui_init();       // UI 启动
    app_webui_init();    // WebUI 启动
    app_cli_init();      // CLI 启动

    // 4. 进入事件循环（或让 FreeRTOS 调度器接管）
    service_mgr_start();
}
```

---

## 五、重构路线图（分阶段）

### Phase 1：基础设施（1-2 天）

1. ✅ 引入事件总线（event_bus）
2. ✅ 封装 disp_driver 的全局变量
3. ✅ 封装 wifi_manager 的全局变量
4. ✅ 建立 UI state 管理

**验收标准**：不再有模块通过 `extern` 引用其他模块的变量

### Phase 2：音频子系统重构（2-3 天）

1. 新建 `audio_hal` 组件
2. 将 codec 初始化从 radio 迁移到 audio_hal
3. recorder 改为依赖 audio_hal
4. audio_min 改为依赖 audio_hal
5. 删除 audio_bsp

**验收标准**：recorder 不依赖 radio；采样率切换集中管理

### Phase 3：UI 模块拆分（3-5 天）

1. 拆分 ui_settings.c 为多个子页面
2. 建立 ui_state 模块
3. 各 UI 模块的 widget 指针改为 static 内部变量
4. 通过事件总线通信替代直接调用

**验收标准**：ui_*.h 中不再有 `lv_obj_t *` 类型的全局变量

### Phase 4：配置层优化（1-2 天）

1. 从 app_cfg 中移除业务逻辑
2. 引入基于事件的配置变更通知
3. 优化配置 API（减少重复代码）

**验收标准**：app_cfg.c 行数减少 30%；配置变更通过事件通知

### Phase 5：组件清理和重命名（1 天）

1. 删除 audio_bsp
2. 裁剪或删除 SensorLib
3. webui 移到 main/app/
4. radio 重命名为 audio_player_net

**验收标准**：components/ 目录只包含可复用的驱动和库

---

## 六、风险和注意事项

### 6.1 风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 重构期间功能回归 | 高 | 每阶段完成后完整测试；保留旧代码分支 |
| LVGL 非线程安全 | 中 | UI 状态变更统一走 lvgl_lock |
| 事件总线性能开销 | 低 | 事件不多，对 ESP32-S3 可以忽略 |
| 内存占用增加 | 低 | 事件队列深度可配置，占用很小 |

### 6.2 注意事项

1. **不要一次性重构全部**：分阶段进行，每个阶段都保持可编译可运行
2. **保持 API 兼容**：初期可以保留旧 API 作为 wrapper，逐步迁移调用方
3. **充分利用现有测试手段**：CLI 命令 + WebUI + 手动操作验证
4. **关注 FPS**：重构前后对比 FPS，确保没有性能退化
5. **关注内存**：使用 `heap_caps_get_free_size()` 监控内存变化

---

## 七、可立即执行的 Quick Wins

不需要大的架构变更，可以马上做的优化：

1. **删除 `audio_bsp`**：确认未使用，直接删除，减少编译时间
2. **裁剪 `SensorLib`**：检查实际用到了哪些传感器驱动，删除未使用的代码
3. **`disp_driver` 全局变量封装**：改动小，收益大
4. **`wifi_manager` 全局变量封装**：同上
5. **统一 `g_` → `s_`**：对于只在模块内部使用的变量，改为 static + s_ 前缀

---

*文档结束*
