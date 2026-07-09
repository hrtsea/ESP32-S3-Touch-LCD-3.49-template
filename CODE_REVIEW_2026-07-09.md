# 审查意见

> 审查对象：最新一次 commit `4628213` — *feat: add WiFi provisioning timeout mechanism and bridge module*
> 范围：`main/` 下 12 文件，+139 / −106
> 方法：因工作树对 `main/` 无未提交变更，按 code review 流程回退分析最新 commit 的 diff，并结合 `event_bus.c` / `nas_event_loop.c` / `CMakeLists.txt` 上下文交叉验证。

一句话结论：**方向正确、清理到位（凭证管理收敛到 esp_wifi_config、去轮询改事件驱动、新增桥接模块），但引入了一个 `event_bus` 不拥有 payload 指针的脆弱契约、仍把弱默认凭证写死入库、且断连原因不再复位——存在正确性/安全隐患，合入前应加固。**

---

## 建议

### 1. 🔴 `event_bus_publish` 不拷贝 payload，桥接转发的是 esp_bus 回调的临时指针（脆弱的所有权契约）
- 证据：`event_bus_publish`（`event_bus.c:124-150`）把裸 `void *data` 直接放进 `event_t`，并 `xQueueSend(s_event_queue, &evt, 0)` 把**指针本身**入队——payload 从不拷贝。对照 `event_bus_publish_nas_data`（`event_bus.c:152-182`）专门 `memcpy` 到静态 `s_nas_data_buffer`，两者行为不一致。
- 桥接：`wifi_bridge.c:13-15` 的 `bridge_forward` 把 esp_bus 回调给的 `const void *data` 直接转 `(void *)data` 转发。该指针只在 esp_bus 回调上下文内有效。
- 现状：唯一异步消费者 `nas_event_loop`（`nas_event_loop.c:54-74`）对 `EVENT_WIFI_CONNECTED/DISCONNECTED` 只 `switch(evt.id)`、**不**解引用 `evt.data`，所以**今天不崩**。
- 风险：任何未来消费者（或同步 handler）一旦解引用 WiFi 事件的 `evt->data`，就会读到悬空/过期数据。这是一个“靠约定而非靠机制”的雷。
- 建议（任选其一，推荐前者）：
  1. 桥接在转发前把 `wifi_disconnected_t` 拷进静态缓冲（`CONNECTED`/`SCAN_DONE` 无 payload 则传 `NULL, 0`）；
  2. 改造 `event_bus_publish` 统一按 `len` 拷贝 payload（与 `publish_nas_data` 对齐）；
  3. 至少在该契约上 loudly 注释，并明确“WiFi 事件 payload 仅在同步 handler 内有效，禁止经 `event_bus_receive` 异步读取”。

### 2. 🟠 弱默认凭证仍写死入库（`user_config.h`）
- 证据：`user_config.h:60-74` 定义 `DEFAULT_AP_PASSWORD "12345678"`、`WEBUI_AUTH_PASSWORD "admin"`，注释标 `DEV ONLY`。虽有 `#ifndef` 可被 sdkconfig 覆盖，但生产若漏配 `CONFIG_DEFAULT_AP_PASSWORD` / `CONFIG_WEBUI_AUTH_PASSWORD`，设备即以弱密码出厂（SoftAP `12345678`、WebUI `admin/admin`）。
- 肯定：本次把原本散在 `app_cfg.c` 的凭证收敛到单一 `user_config.h`，方向正确。
- 建议：① 编译期/运行期检测若仍是 dev 默认值则拒绝启动或强制进配网；② 或将默认凭证彻底移到 NVS / 本地 `.gitignore` 的 secrets 头；③ `default_ap.password` 长度 8 刚好满足 WPA2 下限，OK，但建议生产强制 ≥12 位。

### 3. 🟠 `wifi_connect` 不再复位 `s_last_reason`，导致陈旧断连原因残留
- 证据：`wifi_adapter.c` 本次删除了 `wifi_connect` 入口的 `s_last_reason = 0;`。
- 影响：一次断开设的 `reason` 会一直保留到下次 disconnect 才更新。WifiTab 在发起连接时即显示 `reason N`（`ui_Screen_Settings_WifiTab.c:122-127`），用户可能看到上一次会话的陈旧错误码，造成误导。
- 建议：在 `wifi_connect` 入口复位 `s_last_reason = 0`（或在 disconnect handler 内才 set，connect 成功路径清 0）。

### 4. 🟡 `WIFI_PROV_ON_FAILURE` + 重连耗尽重启 的恢复路径需真机验证
- 证据：`main.cpp:69-90` 设置 `provisioning_mode = WIFI_PROV_ON_FAILURE`、`max_reconnect_attempts = 5`、`retry_interval_ms=5000`、`retry_max_interval_ms=30000`、`on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART`。
- 风险：路由器真不可达时链路为 5 次重连（5s→30s 退避）→ 重启 → boot → 失败 → 再重启…… 直到 `WIFI_PROV_ON_FAILURE` 进 SoftAP 配网。需确认重启后能**及时**出现 SoftAP 供用户改配置，否则存在分钟级 boot-loop 空窗且用户无入口。
- 建议：拔掉路由器做实测，记录“首次 SoftAP 出现时间”与是否真的进入配网；评估是否需要在耗尽前先短暂起 SoftAP 再决定是否重启。

### 5. 🟡 `main/ui/old/` 引用了已删除的 API（死代码陷阱）
- 证据：`main/ui/old/ui_quotes.c`、`ui_wifi_config.c`、`ui_settings.c` 共 8 处仍调用 `wifi_get_curr_ssid` / `wifi_reason_str`（`wifi_bridge.c` 提交已删除二者）。当前 `CMakeLists.txt` 未把 `old/` 编入（`SRCS` 为显式列表，无 GLOB），故不破坏构建。
- 风险：一旦有人把 `old/` 加回即编译失败；且是维护陷阱（代码看起来“还在用”，实际 API 已变）。
- 建议：删除 `main/ui/old/` 目录，或在目录内放 `README` 明确标注“废弃、请勿编译”。

### 6. 🔵 轻微项
- **const 正确性**：`wifi_bridge.c:13` `(void *)data` 强去 const 虽注释解释，但破坏 const 契约；更干净的做法是让 `event_bus_publish` 形参改为 `const void *`。
- **同步执行上下文**：桥接使 `event_bus` 的 WiFi handler 在 **esp_bus 回调任务内同步执行**（`event_bus_publish` 第 141-145 行直接调 handler）。若某 UI handler 内有阻塞（如 LVGL 锁等待），会阻塞 esp_wifi_config 的事件分发任务——建议确认所有 WiFi handler 非阻塞或快速返回。
- **日志级别**：`on_connected/on_disconnected/on_scan_done` 每个事件都 `ESP_LOGI`（`wifi_bridge.c:18-45`），断连/重连风暴会刷屏，建议降为 `ESP_LOGD` 或加节流。
- **去轮询的 UX 缺口**：`ui_Screen_Settings_WifiTab.c` 删除了 1s 轮询定时器（`s_wifi_status_timer_cb`），改纯事件驱动——方向对，但若 connect 发起后既无 connected 也无 disconnected 事件（极端卡死），15s “timed out” 状态不会刷新。建议 `wifi_connect` 后补一个一次性超时定时器，或依赖 esp_wifi_config 的超时事件。

---

**注意：** 本提交**当前可编译可运行、无活动崩溃**（nas_event_loop 不解引用 WiFi 事件 payload，故第 1 条不会立即触发）；但第 1、2、3 条属于“现在不炸、重构即炸/出厂即弱”的隐患，应在合入前处理，尤其不要把 `main/ui/old/` 误编回构建。
