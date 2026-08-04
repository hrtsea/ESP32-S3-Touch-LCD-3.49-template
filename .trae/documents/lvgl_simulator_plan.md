# LVGL PC 模拟器构建计划

## Context

项目是一个 ESP32-S3 + LVGL 8.3.11 的 NAS 监控屏项目，UI 分辨率 640x172。当前 UI 迭代必须烧录到硬件才能看到效果，开发效率低。目标是构建一个 PC 端 LVGL 模拟器，复用项目现有 UI 源码，用 mock 数据驱动，在 Windows 上直接预览所有屏幕，加速 UI 调试。

**用户决策**：全部屏幕一次到位（含 WifiConfig）+ 复用现有 mock\_client.c 定时轮询。

**环境已确认**：

* MSYS2 MinGW64 工具链完整：gcc 15.2.0, cmake 4.3.1, mingw32-make 4.4.1

* SDL2 开发库齐全：`C:\msys64\mingw64\include\SDL2\`、`libSDL2.a`、`libSDL2main.a`、`SDL2.dll`

* LVGL `custom.cmake` 支持 PC 构建（通过 `LV_CONF_PATH` + `add_subdirectory`）

* `esp_wifi_config.h` 位于 `managed_components/thorrak__esp_wifi_config/include/`

## 核心策略：双层隔离

| 耦合层         | 处理策略                       | 示例                                                    |
| ----------- | -------------------------- | ----------------------------------------------------- |
| 头文件级（类型/宏）  | `sim/stubs/*.h` 空实现        | esp\_log.h, freertos/\*.h, driver/ledc.h              |
| API 级（函数调用） | `sim/stubs/stubs.c` 集中实现   | wifi\_cfg\_*, nvs\_*, esp\_timer\_get\_time           |
| 模块级（深度依赖）   | `sim/overrides/*.c` 替换原 .c | disp\_driver.c(SDL后端), event\_bus.c(同步), config.c(内存) |

**零修改原则**：不修改 `main/` 下任何源码，所有适配通过 stub/override 在 `sim/` 完成。UI 源码直接通过 CMake 路径引用编译。

## 目录结构

```
sim/
├── CMakeLists.txt              # 顶层 CMake（MinGW + SDL2 + LVGL）
├── lv_conf.h                   # PC 版 LVGL 配置（640x172, RGB565, 字体全开）
├── main.c                      # SDL+LVGL 入口 + mock 数据驱动循环
├── README.md                   # 构建运行说明
│
├── stubs/                      # ESP-IDF 头文件 stub
│   ├── CMakeLists.txt
│   ├── stubs.c                 # 所有 stub 函数集中实现
│   ├── esp_log.h               # ESP_LOGI/E/W/D → fprintf(stderr)
│   ├── esp_err.h               # esp_err_t, ESP_OK/FAIL 枚举
│   ├── esp_system.h            # esp_log_timestamp() → clock()
│   ├── esp_timer.h             # esp_timer_get_time() → clock()
│   ├── esp_heap_caps.h         # heap_caps_malloc → malloc
│   ├── esp_wifi.h              # 空
│   ├── esp_wifi_types.h        # wifi_auth_mode_t 枚举
│   ├── esp_wifi_config.h       # 复制原头文件（去注释，保留类型/声明）
│   ├── esp_http_server.h       # httpd_handle_t/req_t 不透明指针
│   ├── esp_netif.h, esp_event.h, esp_sntp.h, esp_console.h, esp_vfs_fat.h  # 空
│   ├── esp_lcd_panel_io.h, esp_lcd_panel_vendor.h, esp_lcd_panel_ops.h    # 空
│   ├── esp_lcd_axs15231b.h, esp_io_expander_tca9554.h, esp_bus.h           # 空
│   ├── nvs_flash.h, nvs.h      # nvs_open → ESP_ERR_NVS_NOT_FOUND
│   ├── argtable3/argtable3.h   # 空
│   ├── driver/
│   │   ├── ledc.h              # LEDC_TIMER_2 等宏 + ledc_timer_config_t 结构
│   │   ├── pcnt.h              # PCNT_UNIT_0 等宏
│   │   ├── spi_master.h        # SPI3_HOST 宏
│   │   ├── gpio.h              # GPIO_NUM_* 宏
│   │   └── i2c_master.h        # 空
│   ├── freertos/
│   │   ├── FreeRTOS.h          # TickType_t, portMAX_DELAY, pdMS_TO_TICKS
│   │   ├── task.h              # xTaskCreate → pdFAIL, vTaskDelay → 空
│   │   ├── semphr.h            # SemaphoreHandle_t=void*, xSemaphore* → pdTRUE
│   │   └── queue.h             # xQueueSend/Receive → pdFAIL
│   ├── bsp/
│   │   ├── lcd_bl_pwm_bsp.h    # setUpduty()
│   │   ├── sdcard_bsp.h        # sdcard_is_mounted() → false
│   │   ├── i2c_bsp.h, i2c_equipment.h, button_bsp.h, adc_bsp.h, audio_min.h  # 空
│   └── clients/
│       └── *.h                 # api_client.h 等 7 个客户端头（声明 create 返回 NULL）
│
├── overrides/                 # 项目模块的 sim 替换实现
│   ├── CMakeLists.txt
│   ├── disp_driver.c          # SDL 后端：lvgl_lock no-op, fps 计数, canvas size
│   ├── event_bus.c             # 单线程同步版：publish 直接调订阅者，receive 返回 false
│   ├── bg_fetcher.c            # no-op
│   ├── fan_control.c           # 内存状态版（get_status 返回固定值）
│   ├── hw_init.c               # no-op
│   ├── cli.c                   # no-op
│   ├── http_timer.c            # 简单计时器 stub
│   ├── nas_event_loop.c        # 简化：直接 publish
│   ├── sntp_manager.c          # no-op
│   ├── config.c                # 仅 config_init_defaults, NVS 部分 no-op
│   ├── app_cfg.c               # 内存版 g_cfg（复制原初始化列表）+ getter/setter
│   └── clients_stub.c          # 7 个非 mock 客户端 create 函数返回 NULL
│
└── sdl/
    ├── sdl_driver.h
    └── sdl_driver.c            # SDL 窗口(1280x344=2x缩放) + RGB565纹理 + 鼠标输入
```

## 关键文件实现要点

### 1. `sim/CMakeLists.txt`（核心构建）

三个 target：

* `sim_stubs` (静态库)：stubs.c + 头文件 include path

* `sim_overrides` (静态库)：所有 override .c + sdl\_driver.c，链接 sim\_stubs + lvgl + SDL2

* `nas_monitor_sim` (可执行)：引用 `main/` 下 UI 源码（不复制）+ sim/main.c，链接 sim\_overrides

**编译的 UI 源码**（直接引用 `${MAIN_DIR}/...`）：

* `ui/ui.c`, `ui/ui_helpers.c`, `ui/ui_events.c`

* `ui/screens/` 下全部 17 个 .c（7 主屏 + 8 Settings Tab + 2 其他）

* `ui/fonts/*.c`（5 个字体）, `ui/images/*.c`（3 个图片）

* `utils/i18n.c`, `utils/theme.c`, `utils/wifi_adapter.c`（复用，依赖 esp\_wifi\_config stub）

* `data/data_source.c`, `data/client/mock_client.c`, `data/nas_data_json.c`（复用）

**不编译**（用 overrides 替换）：disp\_driver.c, event\_bus.c, bg\_fetcher.c, fan\_control.c, hw\_init.c, cli.c, http\_timer.c, nas\_event\_loop.c, sntp\_manager.c, config.c, app\_cfg.c, wifi\_bridge.c
**不编译**（用 clients\_stub.c 替换）：api\_client.c, synology\_client.c, netdata\_client.c, truenas\_client.c, qnap\_client.c, serial\_client.c, snmp\_client.c

**include path 顺序**（关键）：`sim/stubs` > `sim/stubs/bsp` > `sim/stubs/clients` > `main/*` > `components/lvgl`

LVGL 集成：

```cmake
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE PATH "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../components/lvgl lvgl)
```

### 2. `sim/lv_conf.h` 关键配置

* `LV_COLOR_DEPTH 16`, `LV_HOR_RES_MAX 640`, `LV_VER_RES_MAX 172`

* `LV_MEM_CUSTOM 1`（用标准 malloc）

* `LV_TICK_CUSTOM 1` + `SDL_GetTicks()` 作为时基

* 字体全开：`LV_FONT_MONTSERRAT_{12,14,16,18,24,32,48,64,96}` = 1

* Widget 全开（确保屏幕兼容）：arc/bar/btn/canvas/dropdown/keyboard/meter/tabview/tileview 等

* `LV_USE_OS LV_OS_NONE`（单线程）

### 3. `sim/main.c`（入口 + 数据驱动）

```c
int main(int argc, char **argv) {
    config_load();              // g_config 初始化默认值
    app_cfg_init();             // g_cfg 初始化
    event_bus_init();           // sim 同步事件总线

    lv_init();
    sdl_driver_init();         // SDL 窗口 + LVGL display driver + mouse indev

    ui_init();                  // 加载 Boot 屏 + 启动 boot 进度定时器

    data_source_init("mock");  // 复用 mock_client
    data_source_connect();

    while (1) {
        uint32_t now = lv_tick_get();
        sdl_driver_poll();      // SDL 事件（鼠标/退出）
        lv_tick_inc(5);
        lv_timer_handler();

        // 每 poll_sec 秒拉取 mock 数据并驱动屏幕
        if (now - s_last_poll_ms >= g_config.poll_sec * 1000U) {
            s_last_poll_ms = now;
            if (data_source_poll()) {
                const NasData *data = data_source_get_data();
                if (data && data->is_online) {
                    event_bus_publish_nas_data(data);
                    sim_update_screens(data);  // 直接调用各屏幕 update 函数
                }
            }
        }
        SDL_Delay(5);           // ~200fps cap
    }
}
```

`sim_update_screens()` 直接调用各屏幕的 `*_update_*` 函数（绕过失效的 ui\_events 任务）：overview\_screen\_update\_cpu/temp/mem/disk/network/ip/time/hdd\_\*、ui\_Screen\_DiskDetail\_update\_data、ui\_Screen\_SystemDetail\_update\_data 等。

### 4. `sim/sdl/sdl_driver.c`（SDL 显示后端）

* 窗口：1280x344（640x172 的 2 倍缩放）

* 纹理：SDL\_PIXELFORMAT\_RGB565，640x172

* flush\_cb：`SDL_UpdateTexture` + `SDL_RenderPresent`

* 鼠标：坐标除以 SIM\_SCALE 映射到 640x172

* 退出：SDL\_QUIT → exit(0)

### 5. `sim/overrides/event_bus.c`（同步版）

```c
// publish 直接同步调用所有订阅者，receive 永远返回 false
static slot_t s_slots[EVENT_MAX];  // 每事件最多 8 个 handler
void event_bus_publish(event_id_t id, void *data, size_t len) {
    event_t evt = {id, data, len};
    for (int i = 0; i < s_slots[id].count; i++)
        s_slots[id].handlers[i].h(&evt, s_slots[id].handlers[i].ud);
}
```

### 6. `sim/overrides/disp_driver.c`（SDL 后端 stub）

实现 `disp_driver.h` 所有 API：

* `lvgl_lock/unlock` → no-op（单线程）

* `g_fps_frame_count/g_fps_label` → 全局变量

* `disp_driver_fps_timer_cb` → 更新 FPS label

* `g_canvas_w/h` = 640/172

* `webui_snapshot_fb` → 返回 -1

### 7. `sim/overrides/app_cfg.c`（内存版）

从原 `main/config/app_cfg.c` 复制：

* `g_cfg` 的 designated initializer（约 70 字段）

* 所有 getter/setter 函数体（直接读写 g\_cfg，`app_cfg_save` 改 no-op）

### 8. `sim/overrides/config.c`（内存版）

从原 `main/config/config.c` 复制 `config_init_defaults()`，所有 `config_save_*` 改 no-op，`config_load` 调用 `config_init_defaults`。

### 9. `sim/stubs/esp_wifi_config.h`（关键 stub）

**策略**：直接复制 `managed_components/thorrak__esp_wifi_config/include/esp_wifi_config.h` 全部内容（去掉文档注释），保留所有类型定义和函数声明。这样 100% 兼容。

在 `sim/stubs/stubs.c` 提供 mock 实现：

* `wifi_cfg_is_connected` → true

* `wifi_cfg_get_status` → 填充假 SSID/IP/RSSI

* `wifi_cfg_scan` → 返回 3 个假网络

* `wifi_cfg_list_networks` → 返回 2 个假已保存网络

* 其余 → ESP\_OK 或空操作

### 10. `sim/stubs/stubs.c`（集中 stub 实现）

包含所有 ESP-IDF 函数的 mock：

* `esp_timer_get_time` → `clock() * 1000000 / CLOCKS_PER_SEC`

* `esp_log_timestamp` → `clock() * 1000 / CLOCKS_PER_SEC`

* `heap_caps_malloc/free` → `malloc/free`

* `nvs_open/get_*/set_*` → ESP\_ERR\_NVS\_NOT\_FOUND / ESP\_OK

* `wifi_cfg_*` → 假数据

* `fan_control_*` → 固定值（rpm=1200, pwm=40, temp=45）

* `bg_fetcher_ensure` → no-op

* `hw_init`, `system_monitor_start` → no-op

* `http_timer_*` → 简单 stub

* `setUpduty`, `sdcard_is_mounted` → no-op / false

## 关键挑战与应对

1. **`ui.h`** **依赖链**：ui.h include 14 个项目头 + 7 个屏幕头。不创建替代 ui\_sim.h，而是确保所有 stub 头文件在 include path 中优先解析（`sim/stubs` 在前）。

2. **`ui_events.c`** **双重代码**：包含 15 个事件处理器（屏幕引用，必须编译）+ FreeRTOS 任务（sim 中失效）。编译 ui\_events.c 但不调用 `ui_events_start()`，由 sim main.c 直接调用屏幕 update 函数。

3. **`data_source.c`** **引用所有客户端**：stub 7 个客户端头文件 + `clients_stub.c` 提供 create 函数返回 NULL。sim 只走 mock 路径。

4. **符号冲突**：CMakeLists 显式只列 sim override 的 .c，不列原 .c（event\_bus.c、config.c 等）。

5. **`user_config.h`** **硬件宏**：sim override 的 app\_cfg.c 不 include 它，无需 stub。

## 实现步骤

1. **创建骨架**：`sim/` 目录 + CMakeLists.txt + lv\_conf.h + 最小 main.c（SDL 窗口 + LVGL hello world），验证工具链通
2. **LVGL 集成**：`add_subdirectory(components/lvgl)`，验证 lvgl 静态库构建
3. **核心 stub**：esp\_log.h, esp\_err.h, freertos/\*, esp\_timer.h, esp\_system.h, nvs.h
4. **核心 override**：disp\_driver.c(SDL), event\_bus.c(同步), config.c, app\_cfg.c, bg\_fetcher.c, fan\_control.c
5. **打通 Boot 屏**：编译 ui.c + ui\_Screen\_Boot.c + ui\_helpers.c + ui\_events.c，验证显示
6. **接入 mock 数据**：data\_source.c + mock\_client.c + nas\_data\_json.c + i18n.c + theme.c
7. **打通 Overview 屏**：sim main.c 直接驱动 `overview_screen_update_*`
8. **逐屏接入**：Storage → DiskDetail → SystemDetail → Settings(8 Tab) → WifiConfig
9. **完善 WiFi stub**：复制 esp\_wifi\_config.h，提供完整 mock（scan/list/status）
10. **触摸交互验证**：鼠标点击按钮、tabview 切换、tileview 滑动、键盘输入
11. **打磨**：FPS 显示、退出处理、README

## 验证步骤

1. **构建**：在 MSYS2 MinGW64 终端执行

   ```bash
   cd /e/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/sim
   mkdir build && cd build
   cmake -G "MinGW Makefiles" ..
   mingw32-make -j8
   ```
2. **运行**：`./nas_monitor_sim.exe`（需要 SDL2.dll 在同目录或 PATH）
3. **验证清单**：

   * [ ] SDL 窗口显示 640x172 内容（2x 缩放为 1280x344）

   * [ ] Boot 屏显示启动进度

   * [ ] Overview 屏显示 CPU/内存/磁盘/温度，数据每 5 秒变化

   * [ ] 鼠标点击 Overview 的硬盘区域 → 跳转 DiskDetail

   * [ ] DiskDetail 显示硬盘详情（Name/Model/Type/Health/温度/读写速度）

   * [ ] 鼠标点击 CPU/内存区域 → 跳转 SystemDetail

   * [ ] SystemDetail CPU 模式显示核心条，MEM 模式显示 RAM/Swap/Disk 条

   * [ ] Storage 屏显示所有硬盘槽位（6 SATA + 3 M.2）

   * [ ] Settings 屏 Tab 切换正常（NasTab/ScreenTab/FanTab 等）

   * [ ] FanTab 风扇曲线显示，可点击编辑

   * [ ] WifiConfig 屏显示假网络列表

   * [ ] 手势滑动可返回上一屏（鼠标拖拽模拟）

   * [ ] FPS 计数显示在左上角

   * [ ] 关闭窗口正常退出

## 风险

1. **esp\_wifi\_config.h 体量大**：原头文件 >500 行（含注释），复制后 stubs.c 需实现约 30 个 wifi\_cfg\_\* 函数。按需实现，未用到的可返回 ESP\_OK/NULL。
2. **ui\_events.c 的静态数据更新函数**：原版通过 FreeRTOS 任务调用 `overview_screen_update_*`。sim 中这些函数仍存在但无人调用，需由 sim main.c 主动调用。需确认 ui\_events.c 中是否有屏幕初始化时注册的回调依赖任务上下文。
3. **图片资源**：`ui_img_images_*.c` 是 LVGL 内置图片格式（C 数组），不需 PNG 解码器。但若屏幕引用了外部 PNG 文件路径，需 stub `lv_fs_*`。
4. **MinGW 链接顺序**：`SDL2main` 必须在 `SDL2` 前，且需要 `-mwindows` 或 `-mconsole` 控制台模式。

