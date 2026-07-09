---
name: wifi_event_bridge_phase1
overview: Phase 1 优化：新建 wifi_bridge 模块将 esp_wifi_config 的 esp_bus 事件（connected/disconnected/scan_done/got_ip）桥接到项目自研 event_bus，并移除 UI 层（ui_Screen_Settings_WifiTab）的 1 秒轮询定时器，改为纯事件驱动刷新。
todos:
  - id: create-wifi-bridge
    content: 新建 wifi_bridge.h/.c，订阅 esp_bus 事件并转发到 event_bus
    status: completed
  - id: wire-bridge-init
    content: 在 main.cpp 的 wifi_cfg_init 后调用 wifi_bridge_init
    status: completed
    dependencies:
      - create-wifi-bridge
  - id: remove-ui-polling
    content: 移除 Wi-Fi 标签页轮询定时器，保留事件订阅与一次性刷新
    status: completed
  - id: verify-build
    content: 编译验证无新增错误，确认事件链路打通
    status: completed
    dependencies:
      - create-wifi-bridge
      - wire-bridge-init
      - remove-ui-polling
---

# ESP event bus 对比分析：ESPToolKit/esp-eventbus vs 本项目自研 event_bus

> 目标：对比第三方库 `ESPToolKit/esp-eventbus`（v1.0.1, MIT）与本项目 `main/utils/event_bus.{h,c}` 的设计、优缺点，评估是否值得迁移。

---

## 1. 两者机制速览

### 1.1 本项目 `event_bus`（自研，C 语言）

- **模型：同步扇出 + 队列旁路（混合）**
  - `event_bus_publish(id, data, len)` 在**发布者上下文中**立即 `for` 循环调用该 event 的全部 handler（`event_bus.c:141-145`）。
  - 同时 `xQueueSend(s_event_queue, &evt, 0)` 投递一份到队列（`event_bus.c:147`）。
  - 唯一队列消费者是 `nas_event_loop` 任务（`nas_event_loop.c:48`），仅用于「触发 HTTP 抓取 / WiFi 启用抓取」等控制流。
- **订阅表**：固定数组 `s_slots[EVENT_MAX]`，每事件最多 `MAX_HANDLERS_PER_EVENT=8`（`event_bus.c:12,24`）。
- **事件 ID**：强类型 `enum event_id_t`，含名称表 `s_event_names[]` 便于日志。
- **线程安全**：单个全局 `s_mux` 互斥量保护订阅表；发布时复制 slot 后释放锁再调 handler（避免持锁调回调）。
- **ISR 安全**：`event_bus_publish` 用 `xSemaphoreTake(s_mux, portMAX_DELAY)`——**非 ISR 安全**，不能在中断里调用。
- **特殊通道**：`event_bus_publish_nas_data()` 用独立 `s_nas_data_mux` 把 `NasData` 拷贝进 `s_nas_data_buffer` 再发布，保证负载生命周期。

### 1.2 ESPToolKit/esp-eventbus（第三方，C++17）

- **模型：纯异步**
  - `post(id, payload, timeout)` 仅把 `{id, payload指针}` 入队，立即返回；独立 **worker 任务**（`xTaskCreatePinnedToCore`）异步扇出执行所有订阅回调。
  - `postFromISR(...)` 提供 ISR 安全入队。
  - `waitFor(id, timeout)` 提供**同步阻塞**等待下一匹配事件（每 task/event 复用等待队列，免堆抖动）。
- **订阅**：`std::function` 回调，可绑类方法/捕获 lambda；支持 `oneshot`；不限数量（受 `maxSubscriptions` 配置）。
- **配置 `EventBusConfig`**：`queueLength`(默认16)、`priority`、`stackSize`、`coreId`、`overflowPolicy`(Block/DropNewest/DropOldest)、`pressureThresholdPercent`、`pressureCallback`、`dropCallback`、`payloadValidator`、`usePSRAMBuffers`。
- **内存**：只存指针不拷负载（用户保证生命周期）；可选 PSRAM 缓冲。
- **限制**：仅 ESP32 + C++17 + FreeRTOS；休眠/重启前须 `deinit()`。

---

## 2. 逐项对比

| 维度 | 本项目 event_bus | esp-eventbus |
|------|------------------|--------------|
| 语言 | C（兼容 ESP-IDF C 入口） | C++17（需 `.cpp` 或 extern C 包装） |
| 执行模型 | **同步扇出**（发布者上下文跑 handler） | **异步**（worker 任务扇出） |
| 发布者阻塞 | handler 慢 → 发布者被拖慢 | 发布者零等待（Block 策略除外） |
| ISR 安全 | ❌ 否（`portMAX_DELAY` 取锁） | ✅ `postFromISR` |
| 订阅容量 | 每事件固定 8 个 | 可配置，近乎无限 |
| 回调类型 | C 函数指针 `(evt, user_data)` | `std::function`（lambda/成员方法） |
| 负载生命周期 | 调用方保证（nas_data 走拷贝缓冲） | 调用方保证（不拷贝） |
| 溢出/背压 | 队列满 `xQueueSend(...,0)` 静默丢 | 可配 Drop/Block + 压力回调 |
| 同步等待 | 仅 `nas_event_loop` 用队列收 | `waitFor` 通用，每 task 复用队列 |
| 可观测性 | 名称表 + ESP_LOGD | 压力/丢弃回调钩子 |
| PSRAM | 无 | 可选 |
| 依赖 | 仅 FreeRTOS | FreeRTOS + C++ STL + ESPBufferManager |
| 集成成本 | 已在用，316+ 引用点 | 需包装层对接 C 代码 |

---

## 3. 本项目的优点

1. **零拷贝、零额外任务切换**：同步扇出对「UI 事件刷新」这种短平快 handler 最省资源——发布即执行，无队列延迟、无 worker 唤醒开销。UI 在 `lvgl_lock` 内收事件，天然串行，无需担心并发。
2. **C 语言、与现有代码零摩擦**：全项目 316+ 处引用都是 C 风格 `event_bus_publish/subscribe`，无 C++ 异常/STL 负担。ESP-IDF 的 `main` 虽可混 C++，但大量 `.c` 文件若改 C++ 编译会牵动编译配置与符号。
3. **强类型事件枚举 + 名称表**：`event_id_t` 编译期检查，`event_bus_name()` 直接打印可读名，调试友好。
4. **混合队列的务实设计**：同步扇出覆盖绝大多数即时刷新；队列旁路专门服务 `nas_event_loop` 的「控制流解耦」（把抓取触发从发布者任务挪到专用任务，避免阻塞发布者）。
5. **简单、可预测、易审计**：单文件 ~195 行，无动态分配、无 C++ 模板，内存恒定。

---

## 4. 本项目的缺点（esp-eventbus 能补的）

1. **非 ISR 安全**：`portMAX_DELAY` 取互斥量在中断上下文会崩。若将来要从 ISR 发事件（GPIO 中断、定时器 ISR），需另写路径。esp-eventbus 的 `postFromISR` 直接解决。
2. **发布者被 handler 拖慢（同步耦合）**：`event_bus_publish` 在发布者栈内跑所有 handler。若某 handler 做重活（阻塞 I/O、复杂解析），发布者任务（可能是 Wi-Fi 回调任务、LVGL 任务）被卡。esp-eventbus 的 worker 模型把重活离岸。
   - 现状缓解：项目约定「事件 handler 内不做重活」，且 `nas_event_loop` 已把抓取挪到队列消费者，故当前未触发。但属架构隐含约束，靠纪律维持。
3. **队列旁路语义不清晰**：`s_event_queue` 同时被所有事件 `xQueueSend`，但只有 `nas_event_loop` 一个消费者按 `evt.id` 过滤。其他事件入队后无人消费——即除 NAS 控制流外，入队是**无效开销/潜在丢失**。队列实质只为 `EVENT_TRIGGER_HTTP_FETCH` / `EVENT_WIFI_*` 三类服务，通用性差。
4. **订阅上限硬 8 个**：单事件超 8 订阅静默失败（仅 WARN 日志），无配置余地。
5. **无背压/溢出策略**：队列满 `xQueueSend(...,0)` 直接丢，无 Drop 策略、无监控钩子，故障静默。
6. **无 oneshot / 无 waitFor**：消费者只能被动订阅或单一阻塞收队列，缺少「等一次事件再继续」的通用原语（esp-eventbus 的 `waitFor` 很适合顺序化初始化流程）。

---

## 5. esp-eventbus 的优点（对本项目的价值）

1. **真正的异步解耦**：worker 任务扇出，发布者永不被 handler 拖慢——契合本项目「loop()/任务中不长时间阻塞」的可靠性准则，比同步扇出更稳。
2. **ISR 安全**：可从中断直接 post，扩展性好。
3. **溢出/背压/压力监控**：`overflowPolicy` + `pressureCallback` + `dropCallback` + `payloadValidator`，对噪声发布者可观测、可防雪崩。
4. **std::function + 无限订阅 + oneshot + waitFor**：表达力强，适配复杂状态机与顺序初始化。
5. **PSRAM 支持**：大负载场景可降内部 RAM 压力。

---

## 6. esp-eventbus 的缺点（对本项目的成本）

1. **C++17 强制**：项目主体是 C（`.c` 文件、`extern "C"` 接口）。引入需把 event_bus 调用方改为 C++ 或写 C 包装层，牵动编译与符号，违反「最小改动」。
2. **仅存指针不拷负载**：与本项目现状一致（都要求调用方保活），但 esp-eventbus 无 `nas_data` 式的拷贝缓冲特例——需自行保证每个 payload 生命周期，迁移 `event_bus_publish_nas_data` 的拷贝语义要重写。
3. **多一层任务与队列**：小事件（UI 刷新）走 worker 反而增加延迟与上下文切换；对「即时刷新」诉求不如同步扇出利落。
4. **deinit 责任**：休眠/重启前必须 `deinit()`，否则资源泄漏；项目当前 event_bus 无此约束。
5. **学习/维护外部依赖**：第三方库 API 演进、版本锁定（需 idf_component 管理），不如单文件自研可控、可审计。
6. **worker 长回调仍会阻塞其他订阅**：库自身也注明「长回调阻塞 worker」，并非银弹——本项目的「handler 内不做重活」纪律在两边都要守。

---

## 7. 结论与建议

**不建议整体迁移到 esp-eventbus。** 理由：

- 本项目 event_bus 的**同步扇出 + 混合队列**已适配其实际负载特征（短平快 UI 刷新 + 专用 NAS 控制流任务），且全项目 316+ 引用点为 C 风格，迁移到 C++17 库的成本与风险远超收益。
- esp-eventbus 的核心优势（异步解耦、ISR 安全、背压）在本项目**当前未成为瓶颈**：Wi-Fi 桥接已在 LVGL 锁内转发、NAS 抓取已离岸到 `nas_event_loop`、无 ISR 发布需求。

**建议的精准改进（保留自研、补其短板，符合 Karpathy 最小改动）：**

1. **ISR 安全（如需）**：仅当有中断源要发事件时，新增 `event_bus_publish_from_isr()`（用 `xSemaphoreTakeFromISR` + 队列投递，不在 ISR 内同步扇出）。当前无需求则不做。
2. **消除无效入队**：把 `s_event_queue` 的通用 `xQueueSend` 收口，仅对真正需要异步旁路的三类事件（`EVENT_TRIGGER_HTTP_FETCH` / `EVENT_WIFI_CONNECTED` / `EVENT_WIFI_DISCONNECTED`）入队，或明确队列仅服务 NAS 控制流——避免其他事件静默入队又无人消费。
3. **订阅上限可调**：把 `MAX_HANDLERS_PER_EVENT=8` 提升或改为可配置，避免未来静默丢订阅。
4. **队列溢出可见**：`xQueueSend(...,0)` 失败时记 WARN（已有 ESP_LOGW 基础设施），避免静默丢事件。

以上 4 项均为本项目内小改，无需引入 C++ 依赖，即获得 esp-eventbus 大部分「可观测/防丢/ISR」收益。

---

## 8. 解耦前置改造清单（若要将 event_bus 改为纯异步队列模型）

> 结论：当前**不能**简单"去掉同步扇出、全部入队"。必须先完成以下两类改造，否则纯异步化会引入悬垂指针 + LVGL 并发竞态的必崩问题。本清单为落地前置条件，非立即执行项。

### 8.1 障碍一：栈负载悬垂指针（⚠️ 致命）

以下发布点的 `data` 指向**栈上局部变量**，同步模型下 handler 在发布者栈内立即执行故安全；纯异步下发布函数返回后栈回收 → worker 读到悬垂指针崩溃。

| 事件 | 发布点 | 栈变量 | 当前保活方式 |
|------|--------|--------|--------------|
| `EVENT_ROTATION_CHANGED` | `disp_driver.c:520` | `int s` | 栈 ❌ |
| `EVENT_BACKLIGHT_CHANGED` | `ui_helpers.c:71` | `int s` | 栈 ❌ |
| `EVENT_BACKLIGHT_CHANGED` | `app_cfg.c:670` | `&g_cfg.brightness` | 全局 ✅（安全） |
| `EVENT_BACKLIGHT_CHANGED` | `ui_settings.c:562` | `&g_cfg.brightness` | 全局 ✅（安全） |
| `EVENT_TILE_CHANGED` | `app_cfg.c:729` | `int idx` | 栈 ❌ |
| `EVENT_CFG_CHANGED` | `app_cfg.c:102` | `cfg_change_info_t info` | 栈 ❌ |
| `EVENT_SHOW_FPS_CHANGED` | `app_cfg.c:766` | `uint8_t v` | 栈 ❌ |
| `EVENT_AUDIO_VOLUME_CHANGED` | `ui_settings.c:662` | `uint8_t vol` | 栈 ❌ |
| `EVENT_WIFI_CONNECTED/DISCONNECTED/SCAN_DONE` | `wifi_bridge.c:23,35,43` | 库回调 `const void*` | 库内部缓冲（需确认生命周期覆盖到消费）⚠️ |

**改造要求**：所有 ❌ 项改为静态缓冲或入队时深拷贝。参照现有安全范本 `event_bus_publish_nas_data()`（`event_bus.c:152-182`）的 `s_nas_data_buffer` 拷贝模式——为每类小负载引入静态/环形缓冲，或在 `event_t` 内联存储 `data_len` 字节副本（需扩大队列元素尺寸）。

### 8.2 障碍二：UI handler 锁上下文不一致（⚠️ 必崩）

扫遍 9 个订阅点，handler 是否在 `lvgl_lock` 内执行**无统一约定**：

**A 类 — 已自管锁（异步安全）：**
- `s_on_wifi_event` @ `ui_Screen_Settings_WifiTab.c:144`：显式 `lvgl_lock(100)`…`lvgl_unlock()` ✅

**B 类 — 无锁裸操作 LVGL（异步必崩）：**
- `on_backlight_changed_evt` @ `ui_events.c:126` → `ui_helpers_backlight_apply` 裸调 LVGL
- `on_show_fps_changed_evt` @ `ui_events.c:136` → `lv_obj_clear_flag`/`add_flag` 无锁
- `on_tile_changed_evt` @ `ui_events.c:148` → `lv_obj_set_tile_id` 无锁
- `s_on_wifi_event` @ `ui_Screen_WifiConfig.c:101` → `wifi_config_refresh_status()` + `lv_timer_create` 无锁
- `on_nas_data_update_evt` @ `ui_events.c:177` 及其调用的状态刷新（裸刷 LVGL 控件）

**C 类 — 发布者上下文风险：**
- `wifi_bridge` 在 esp_bus 任务（非 LVGL 线程）同步扇出，B 类 handler 当前已在 esp_bus 任务裸跑 LVGL——属**既有隐患**，纯异步化会放大为确定性崩溃。

**改造要求（二选一，推荐方案 1）：**
1. **每个 UI handler 自管 `lvgl_lock`**（统一为 A 类）：给 B 类 5 处补锁。event_bus 内核保持"发布即入队、worker 扇出"纯异步，不为 handler 管锁（单一职责）。
2. worker 扇出时按事件类别统一包 `lvgl_lock`：但 A 类已自管锁会死锁，需区分"已锁/未锁"语义，复杂度高，不推荐。

### 8.3 落地步骤（有序依赖，不可合并）

```
步骤 1：消除栈负载悬垂（8.1）
  → 改 disp_driver.c / app_cfg.c(TILE/CFG/SHOW_FPS) / ui_settings.c(AUDIO_VOL) 的栈变量为静态或拷贝缓冲
  → 验证：所有 event_bus_publish 的 data 在消费时仍有效
步骤 2：统一 UI handler 锁（8.2，方案 1）
  → 给 B 类 5 处补 lvgl_lock/lvgl_unlock
  → 验证：LVGL 对象仅在有锁上下文被访问
步骤 3：反转发布/扇出职责（纯异步化）
  → event_bus_publish 改为仅 xQueueSend（满则 WARN）
  → 新增 event_dispatch_task 消费队列并扇出（取代 nas_event_loop 的收队列逻辑，或与其合并）
  → init 中创建 dispatch 任务
  → 验证：编译通过；UI 刷新延迟 ≤1 tick；无 LVGL 竞态日志
```

> 注意：`EVENT_WIFI_*` 经 `wifi_bridge` 转发，步骤 3 后 handler 在 dispatch 任务运行；其负载来自库回调，须确认 `wifi_disconnected_t` 等结构体生命周期覆盖到 dispatch 消费（必要时 bridge 层做拷贝）。

### 8.4 当前建议

- **暂不执行 8.1–8.3**。当前同步模型对短平快 UI 事件最优，且 B/C 类无锁 handler 在低频事件下"碰巧能跑"。
- **可立即执行零风险项**（见第 7 节建议）：消除无效入队（仅 NAS 三类旁路）+ 队列溢出 WARN。不动锁语义、不动负载生命周期。

---

## 附：关键代码定位

- 本项目同步扇出：`main/utils/event_bus.c:124-150`（`event_bus_publish`）
- 本项目队列旁路消费者：`main/data/nas_event_loop.c:40-80`
- 本项目 nas_data 拷贝缓冲：`main/utils/event_bus.c:152-182`
- esp-eventbus API：`ESPEventBus::post/postFromISR/subscribe/waitFor`，`EventBusConfig` 见仓库 README
