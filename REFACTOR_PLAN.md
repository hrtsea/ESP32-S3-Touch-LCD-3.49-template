# 项目重构与最佳实践计划（PLAN）

> 基于对整个 `main/` 目录的逐文件通读与调用关系交叉核实，给出独立于项目内注释规则的架构判断。
> 核心思想：**先接上已建好却未连线的设施，再删死代码，最后做简化**；正确性优先，最小侵入。

---

## 0. 现状诊断（已核实的关键事实）

### 0.1 两套配置，其一在启动时不加载（P0 正确性 Bug）
- `g_cfg`（命名空间 `"cfg"`，`config/app_cfg.c`）：时钟/行情/显示/主题，由 `app_main → app_cfg_load()` 加载。✅ 正常。
- `g_config`（命名空间 `"nasmon"`，`config/config.c`）：NAS 数据源连接参数（type/ip/port/user/pass…）。
  - **`config_load()` 全工程零调用**（已 grep 确认）。`app_main` 只调 `app_cfg_load()`，从不调 `config_load()`。
  - 后果：`g_config` 永远是零初始化，`nas_event_loop_start()` 读到空 `nas_type` → 永远回退到 mock。
  - 用户在 `ui_Screen_Settings_NasTab.c` 保存的 NAS 配置（`config_save_nas`）写入 NVS 却**重启后不生效**。这是功能性 Bug。

### 0.2 事件总线已建好，但 UI 侧订阅从未连线（核心架构扭曲）
- `ui_events_subscribe_events()` / `ui_events_start_dim_timer()` / `ui_events_start_tile_monitor()` / `ui_events_register_screen_events()` / `ui_events_wifi_status_cb()` **全工程零调用**（仅定义 + 头声明）。
- 因此：
  - `on_nas_data_update_evt` / `on_backlight_changed_evt` / `on_show_fps_changed_evt` / `on_tile_changed_evt` / `on_cfg_changed_evt` 全部**从未注册** → 这些事件发布后无人处理。
  - **自动调光功能事实上是死的**（dim 定时器从未启动）。
  - 实际工作在跑的 UI 刷新，是 `ui_Screen_Overview` 的 `update_timer_cb`（每 5s 直接 `data_source_get_data()` 轮询）这类**每屏轮询定时器**。
- 结论：项目同时存在「事件驱动」和「轮询」两套范式，事件那套是半成品死代码，轮询那套是真干活但重复、分散。

### 0.3 `app_cfg.c` 的遗留回调也是空的
- `s_callbacks.on_backlight_changed` / `on_bg_fetch_ensure` 全工程**只被读取、从未被赋值**（grep 确认）。
- 所以 `app_cfg_set_brightness()` 里 `s_callbacks.on_backlight_changed(...)` 是 no-op；亮度真正靠 `ui_helpers_backlight_apply()`，而它只在（已死的）dim 定时器与 `ui_helpers_notify_activity()` 里被调用。启动后亮度未被应用。

### 0.4 event_bus 队列是半残设计
- `event_bus_publish_nas_data()` 把 `EVENT_NAS_DATA_UPDATE` 推入 `s_event_queue`，但 `nas_event_loop` 只消费 `EVENT_TRIGGER_HTTP_FETCH` / `EVENT_WIFI_*`，**从不通消费 `NAS_DATA_UPDATE`** → 每 5s 白白入队一个事件后丢弃。
- 队列唯一真正的作用：把 esp_timer 的 `EVENT_TRIGGER_HTTP_FETCH` 解耦到 nas 任务（避免阻塞 esp_timer 任务）。这是合理需求，但用「全局所有事件都入队」来实现很粗糙。

### 0.5 每次 setter 同步写 NVS（P1 稳健性）
- `app_cfg_save()` 在每个 `app_cfg_set_*` 里被直接调用 → `nvs_set_*` + `nvs_commit` 同步提交。
- 滑杆拖动类操作（亮度/音量）会高频提交 NVS，造成 Flash 磨损与短时阻塞。
- 注：`CODE_ANALYSIS.md` 声称存在「脏标记 + 1s 防抖定时器」，当前代码**并无此机制**，文档已过时。

### 0.6 死代码与陈旧文件
- `main/ui/old/`（10 个文件：ui_clock/ui_quotes/ui_settings/ui_wifi_config/ui_hello 及其 .h）：SquareLine 生成屏上线后的遗留物，与 `ui/screens/*` 功能重复。CMakeLists 未引用。
- `main/CODE_ANALYSIS.md`：描述已不存在的 `ui_main.c`/`ui_clock.c` 结构，多处与现状不符（如仍写 bg_fetcher 直接调 UI，实际已改事件发布），会误导。
- `config.h` 中 `g_config.ssid` / `g_config.wifipass`：由 `config_save_wifi()` 写入，但 WiFi 真相源是 `esp_wifi_config`。无读取方 → 死字段。
- 大量事件枚举零订阅零发布（`EVENT_AUDIO_*`、`EVENT_QUOTES_CHANGED`、`EVENT_CLOCK_*`、`EVENT_TICK_*`、`EVENT_USER_ACTIVITY`、`EVENT_STORAGE_CHANGED`、`EVENT_ROTATION_CHANGED`、`EVENT_WIFI_SCAN_STARTED`、`EVENT_HTTP_STOP`、`EVENT_WIFI_PROVISION_*` 等）。

### 0.7 关于「栈变量发布会悬垂」的澄清
- 早期担忧栈变量负载悬垂，但**当前是同步扇出**：`event_bus_publish` 在发布者上下文 for 循环调 handler，handler 返回后才返回，栈变量在其生命周期内。因此 `disp_driver_set_rot_state` 的 `int s`、`app_cfg_set_active_tile` 的 `int idx` 等**当前是安全的**。
- 真正风险只在「改为异步队列派发」时才会出现。所以：**在改为异步前，这些栈发布点是安全的；若未来要异步化，必须先把负载改为静态缓冲/深拷贝**（参考现有 `event_bus_publish_nas_data` 用 `s_nas_data_buffer` 保活的做法）。

---

## 1. 我的架构原则（独立于项目注释）

1. **单一范式**：事件总线作为「数据/配置 → UI」的唯一通道；删除每屏轮询定时器这种重复实现。
2. **先接后写**：已存在且正确的设施（event_bus + ui_events 的 handler 实现）先「连线」，不重写。
3. **正确性优先**：先修「NAS 配置不持久」这一功能性 Bug。
4. **删死代码**：`old/`、`CODE_ANALYSIS.md`、零引用枚举、重复字段——不带感情地删。
5. **不盲目升级**：当前同步事件模型对该固件足够，**不引入 esp_event / 第三方 eventbus** 等重架构；除非有明确异步需求。

---

## 2. 阶段计划（每阶段含可验证标准）

### Phase A — P0：修正确性 + 清死代码（低风险，立竿见影）

**A1. 修复 NAS 配置不持久（1 行）**
- 在 `main.cpp` 的 `app_main()` 中、`event_bus_init()` 后、`nas_event_loop_start()` 前，补调用 `config_load();`。
- 验证：烧录 → 在设置页配置 Synology IP/端口/账号 → 重启 → 日志 `nas_event_loop` 应显示 `Creating data source for type: synology` 而非 mock；`data_source_get_data()` 返回真实类型。
- 回滚：删除该行即可。

**A2. 删除 `main/ui/old/` 整个目录**
- 验证：全量 `idf.py build` 通过；grep `ui_clock.h`/`ui_wifi_config.h` 在 `main/` 内无引用（CMakeLists 本就未列）。

**A3. 删除过时的 `main/CODE_ANALYSIS.md`**
- 理由：与现状严重不符，留着会误导后续维护。

### Phase B — P0：把事件总线真正接上 UI（核心解耦）

**B1. 在 `ui_init()`（ui/ui.c）末尾连线事件层**
- 增加调用：
  - `ui_events_subscribe_events();`            // 注册 NAS/背光/FPS/Tile/CFG 事件 handler
  - `ui_events_start_dim_timer();`             // 复活自动调光
  - `ui_events_start_tile_monitor();`          // 复活 tile 监控
  - 在 Boot 屏构建后、`lv_scr_load` 前，对根屏幕调用 `ui_events_register_screen_events(root)`（或确认各屏已在 `screen_init` 内注册）。
- 验证：
  - 改亮度滑块 → 立即生效（不再依赖 dim 定时器）；
  - 空闲超过 `g_cfg.dim_s` → 背光变暗/关闭（自动调光复活）；
  - `EVENT_NAS_DATA_UPDATE` 每 5s 触发 `on_nas_data_update_evt` 更新仪表（串口应有 `Received NAS data update`）。

**B2. 删除 `ui_Screen_Overview` 里与事件重复的轮询块**
- `update_timer_cb`（ui_Screen_Overview.c:327）同时做：时钟、网速、WiFi 图标、IP、**以及 NasData 仪表刷新**。
- 仪表刷新块（约 L394–L440）与 `on_nas_data_update_evt` 完全重复 → 删掉该块，NasData 改由事件驱动。
- 时钟/网速/WiFi/IP 仍由该 5s 定时器刷新（这些没有对应事件，保留合理）。
- 验证：Overview 仪表随数据更新跳动；时钟/网速照常；无重复写同一 widget 的竞争。

**B3. 启动即应用亮度**
- 在 `ui_init()` 连线后加 `ui_helpers_backlight_apply(g_cfg.brightness);`，使开机即按配置亮度，不依赖首次用户活动。

### Phase C — P1：NVS 写入去抖（稳健性）

**C1. `app_cfg` 延迟提交**
- 将 `app_cfg_save()` 的「立即 `nvs_commit`」改为：标记脏 + 由 LVGL 任务内的 1s 周期计时器（或 `lvgl_timer`）统一 `nvs_commit`。
- 高频 setter（亮度/音量）只置脏，不每次提交。
- 提供 `app_cfg_flush()` 供关机/配网前同步落盘（已有声明，落实调用）。
- 验证：连续拖动亮度滑块 30 次 → 串口 `nvs_commit` 调用次数应远小于 30；静置 1s 后仅提交 1 次；断电重启配置保留。

### Phase D — P2：简化与收敛（可选，低优先级）

**D1. 收敛 event_bus 队列**
- 删除 `event_bus.c` 里「所有事件都入队」的通用 `s_event_queue`/`event_bus_receive`/`EVENT_QUEUE_LEN`。
- `nas_event_loop` 改为：自建专用队列 + `event_bus_subscribe(EVENT_TRIGGER_HTTP_FETCH / WIFI_CONNECTED / WIFI_DISCONNECTED / HTTP_STOP)`；订阅回调（在发布者上下文）只把事件类型投入自身队列，nas 任务 draining 队列后执行真正的 fetch。
- 验证：esp_timer 触发 fetch 不阻塞 esp_timer 任务（串口无 `task watchdog`）；`EVENT_NAS_DATA_UPDATE` 不再产生无效入队。

**D2. 删除零引用事件枚举与重复字段**
- 删除 `event_bus.h` 中确认零订阅零发布的枚举项（AUDIO_*/QUOTES_CHANGED/CLOCK_*/TICK_*/USER_ACTIVITY/STORAGE_CHANGED/ROTATION_CHANGED/WIFI_SCAN_STARTED/HTTP_STOP/PROVISION_*），并同步清理 `event_bus.c` 的 `s_event_names` 表。删除前 grep 确认零引用。
- 从 `config.h` 的 `AppConfig` 删除 `ssid`/`wifipass`（死字段），并删 `config_save_wifi()`（无读取方）。

**D3.（可选）统一 UI 刷新服务**
- 将时钟/网速/WiFi/IP 刷新从 `ui_Screen_Overview` 的私有定时器迁到 `ui_events` 的统一计时器，使各屏成为「纯声明式」、刷新逻辑集中。仅当 Phase B 验证稳定后做。

---

## 3. 明确不做（避免过度工程）

- ❌ 不引入 `esp_event` / `esp-eventbus` 第三方库：当前同步扇出对该固件足够，迁移收益低、风险高、且需重写全部 handler 的锁契约。
- ❌ 不重写 SquareLine 生成的 `ui/screens/*`：生成文件视为只读，业务逻辑只在 `ui_events.c` / `ui_helpers.c` 包装层。
- ❌ 不把 `g_cfg` 与 `g_config` 两个命名空间强行合并：二者域不同（UI/显示 vs NAS 数据源），分离合理；仅修「未加载」这一 Bug，不重构 schema。
- ❌ 不为不存在的场景加抽象（重试框架、插件系统等）。

---

## 4. 风险与回滚

| 阶段 | 主要风险 | 缓解 / 回滚 |
|------|----------|-------------|
| A1 | 极微（多加载一次 NVS） | 删一行 |
| A2/A3 | 误删仍被引用的文件 | 删除前 grep 全工程引用；CMakeLists 未列 old/ |
| B1 | 事件 handler 在错误上下文操作 LVGL | 所有 UI handler 已自管 `lvgl_lock`；上线前用串口确认无 `lvgl_lock failed` |
| B2 | 删错 Overview 轮询块导致仪表不刷新 | 保留 NasData 由事件驱动；逐项比对 `on_nas_data_update_evt` 覆盖的 widget |
| C1 | 延迟提交导致断电丢配置 | 保留 `app_cfg_flush()` 在关键节点（配网、关机）调用；浸泡测试 |
| D1 | 队列重构引入 fetch 丢失 | 保留 esp_timer→队列解耦；加 `queue full` 告警日志 |

---

## 5. 建议执行顺序

1. **A1 + A2 + A3**（正确性 + 清场，约 30 分钟，可立即验证）→
2. **B1 + B2 + B3**（接上事件层，让已有架构真正生效）→
3. **C1**（NVS 去抖，防磨损）→
4. **D1/D2/D3**（简化，按稳定性逐步推进）。

> 一句话总纲：**这个项目的「事件驱动」架构其实已经写完了，只是没插电；先插电、再清灰、最后瘦身，比推倒重来更稳更快。**
