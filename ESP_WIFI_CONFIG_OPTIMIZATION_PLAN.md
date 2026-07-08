# esp_wifi_config 使用分析与优化计划

> 创建日期：2026-07-09
> 分析对象：`managed_components/thorrak__esp_wifi_config` 库在项目中的使用方式
> 目标：梳理当前使用方式，识别问题，提出优化建议

---

## 一、当前使用方式总览

### 1.1 初始化配置

位置：[main.cpp:67-96](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/main.cpp#L67-L96)

```c
wifi_cfg_config_t cfg = {
    .provisioning_mode = WIFI_PROV_WHEN_UNPROVISIONED,  // 仅首次配网
    .stop_provisioning_on_connect = true,
    .http_post_prov_mode = WIFI_HTTP_API_ONLY,          // 连接后保留 API
    .default_ap = {
        .ssid = "NAS-Monitor",
        .password = "12345678",
    },
    .enable_ap = true,
    .http = {
        .api_base_path = "/api/wifi",
    },
};
wifi_cfg_init(&cfg);

// 共享 HTTP 服务器给 webui
httpd_handle_t srv = wifi_cfg_get_httpd();
if (srv) {
    webui_start_with_httpd(srv);
} else {
    webui_start();
}
```

### 1.2 架构分层

项目中形成了 **四层调用链**：

```
┌─────────────────────────────────────┐
│  UI 层 (ui_Screen_Settings_WifiTab) │
├─────────────────────────────────────┤
│  app_cfg (配置层)                   │
├─────────────────────────────────────┤
│  wifi_adapter (适配器层)             │
├─────────────────────────────────────┤
│  esp_wifi_config (底层库)            │
└─────────────────────────────────────┘
```

各层职责：

| 层级 | 文件 | 职责 |
|------|------|------|
| UI 层 | [ui_Screen_Settings_WifiTab.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/ui/screens/ui_Screen_Settings_WifiTab.c) | 显示 WiFi 状态、扫描列表、密码输入 |
| 配置层 | [app_cfg.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/config/app_cfg.c) | 保存 WiFi 凭证到独立 NVS 命名空间、触发连接 |
| 适配器层 | [wifi_adapter.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/wifi_adapter.c) | 封装 esp_wifi_config API，缓存扫描结果 |
| 底层库 | esp_wifi_config | 实际 WiFi 管理、配网、HTTP API |

---

## 二、各模块使用细节

### 2.1 wifi_adapter 适配器层

位置：[wifi_adapter.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/wifi_adapter.c)

**当前功能**：
- 扫描结果静态缓存（`s_scan_results[32]`）
- 连接开始时间记录（`s_connect_started_ms`）
- 最后断开原因（`s_last_reason`，但从未赋值）
- 各 API 的简单转发

**存在的问题**：
1. **重复函数**：`wifi_cfg_get_current_ssid()` 和 `wifi_get_curr_ssid()` 功能完全相同
2. **`s_last_reason` 永远为 0**：没有地方更新它，`wifi_reason_str()` 直接返回 "unknown"
3. **扫描阻塞**：`wifi_cfg_scan_adapter()` 直接调用阻塞式 `wifi_cfg_scan()`，在 UI 线程调用会卡住

### 2.2 UI 层使用方式

位置：[ui_Screen_Settings_WifiTab.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/ui/screens/ui_Screen_Settings_WifiTab.c)

**当前方式**：
- **轮询 + 事件双轨制**：既用 `s_status_timer`（1 秒定时器轮询），又订阅 `event_bus` 事件
- 扫描结果通过 `wifi_adapter` 的静态缓存获取
- 状态显示通过 `wifi_cfg_is_connected()` 和 `wifi_cfg_get_current_ssid()` 轮询

**存在的问题**：
1. **轮询浪费资源**：每秒调用一次状态查询，大部分时候无变化
2. **两套事件系统割裂**：`esp_wifi_config` 用 `esp_bus`，项目用自研 `event_bus`，没有桥接
3. **断开原因不显示**：由于 `s_last_reason` 未更新，错误状态无法准确显示

### 2.3 app_cfg 配置层

位置：[app_cfg.c:746-760](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/config/app_cfg.c#L746-L760)

**当前方式**：
- 有独立的 NVS 命名空间 `NVS_NS_WIFI` (`"wifi"`) 存储 WiFi 凭证
- `app_cfg_wifi_connect_save()` 同时做三件事：
  1. 保存到自己的 NVS
  2. 调用 `wifi_cfg_add_network()` 添加到 esp_wifi_config
  3. 调用 `wifi_cfg_connect()` 触发连接

**存在的问题**：
1. **双重存储**：WiFi 凭证同时存在于 `app_cfg` 的 `wifi` 命名空间和 `esp_wifi_config` 自己的 NVS 中
2. **数据可能不一致**：两边都能改，没有同步机制
3. **重复造轮子**：`esp_wifi_config` 已经有完善的多网络管理，app_cfg 又存了一份

### 2.4 CLI 命令行

位置：[cli.c:160-173](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/cli.c#L160-L173)

- `wifi` - 查看当前 SSID
- `wifi_clear` - 清除凭证（调用 `wifi_cfg_factory_reset()`）
- `wifi_connect <ssid> [pass]` - 连接 WiFi

### 2.5 HTTP 服务器共享

✅ **做得好的地方**：正确地共享了 HTTP 服务器
- `esp_wifi_config` 创建 HTTP 服务器
- 通过 `wifi_cfg_get_httpd()` 获取句柄
- `webui_start_with_httpd(srv)` 在同一服务器上注册 webui 路由
- 避免了端口冲突（之前踩过的坑）

---

## 三、当前存在的主要问题

### 问题 1：两套事件系统，没有桥接

| 事件系统 | 所属 | 用途 |
|---------|------|------|
| `esp_bus` | esp_wifi_config | WiFi 连接/断开/扫描等事件 |
| `event_bus` | 项目自研 | 应用层事件（NAS 数据、配置变更等） |

UI 层订阅的是 `event_bus` 的 `EVENT_WIFI_CONNECTED` 等事件，但这些事件**从未由 esp_wifi_config 发出**。需要桥接层。

### 问题 2：WiFi 凭证双重存储

```
NVS 分区：
├── "app" 命名空间 (app_cfg)
│   └── wifi_ac (auto-connect flag)
├── "wifi" 命名空间 (app_cfg 存的凭证)
│   ├── ssid_xxx
│   └── pass_xxx
└── "wifi_config" 命名空间 (esp_wifi_config 自己的)
    ├── networks
    ├── ap_config
    └── variables
```

两边各存一份，没有同步机制，容易不一致。

### 问题 3：配网模式不利于后续重新配置

当前使用 `WIFI_PROV_WHEN_UNPROVISIONED` —— 仅首次启动时配网。
用户后续想换 WiFi 必须：
1. 手动清除凭证（CLI 或设置里）
2. 重启设备
3. 才能重新进入 SoftAP 模式

### 问题 4：扫描是阻塞操作

`wifi_cfg_scan()` 是阻塞调用，在 UI 事件回调中直接调用会卡住 LVGL 渲染。

### 问题 5：未充分利用 esp_wifi_config 的功能

| 功能 | 是否使用 | 说明 |
|------|---------|------|
| 多网络管理 | ❌ 未充分利用 | 只存一个网络，没有多网络故障转移 |
| 自定义变量 (vars) | ❌ 未使用 | 可存储 NAS 配置等应用数据 |
| esp_bus 事件 | ❌ 未桥接 | 项目有自己的 event_bus |
| HTTP Basic Auth | ❌ 未启用 | API 无认证 |
| 重连耗尽策略 | ❌ 未配置 | 默认无限重试 |
| Web UI | ❌ 用了自己的 | 库自带嵌入式 Web UI，但项目用了自己的 webui |

---

## 四、优化建议

### 🔴 高优先级（建议尽快做）

#### 优化 1：桥接 esp_bus 事件到 event_bus

创建一个桥接模块，将 `esp_wifi_config` 的事件转发到项目的 `event_bus`，让 UI 层真正响应事件而非轮询。

**实现思路**：
```c
// wifi_bridge.c — 新建文件
#include "esp_bus.h"
#include "esp_wifi_config.h"
#include "event_bus.h"

static void bridge_connected(const char *event, const void *data, size_t len, void *ctx) {
    event_bus_publish(EVENT_WIFI_CONNECTED, data, len);
}

static void bridge_disconnected(const char *event, const void *data, size_t len, void *ctx) {
    event_bus_publish(EVENT_WIFI_DISCONNECTED, data, len);
}

static void bridge_scan_done(const char *event, const void *data, size_t len, void *ctx) {
    event_bus_publish(EVENT_WIFI_SCAN_DONE, data, len);
}

void wifi_bridge_init(void) {
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_CONNECTED), bridge_connected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_DISCONNECTED), bridge_disconnected, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_SCAN_DONE), bridge_scan_done, NULL);
    esp_bus_sub(WIFI_EVT(WIFI_CFG_EVT_GOT_IP), bridge_got_ip, NULL);
}
```

**收益**：
- UI 层可以去掉轮询定时器，事件驱动更高效
- 断开原因、IP 地址等信息可以实时显示
- 减少 CPU 占用

---

#### 优化 2：统一 WiFi 凭证存储，移除重复的 app_cfg WiFi 存储

**当前**：WiFi 凭证存在两个地方（app_cfg 的 `wifi` NVS + esp_wifi_config 自己的 NVS）

**建议**：
- 以 `esp_wifi_config` 的存储为准（它已经有多网络管理、NVS 持久化）
- 移除 `app_cfg` 中 `NVS_NS_WIFI` 相关代码
- `app_cfg_wifi_connect_save()` 直接调用 `wifi_cfg_add_network()` + `wifi_cfg_connect()`
- 读取凭证通过 `wifi_cfg_list_networks()` / `wifi_cfg_get_network()`

**收益**：
- 消除数据不一致风险
- 减少代码量
- 天然支持多网络

---

#### 优化 3：配网模式改为 ON_FAILURE + 提供手动触发入口

**当前**：`WIFI_PROV_WHEN_UNPROVISIONED`（仅首次配网）

**建议**：改为 `WIFI_PROV_ON_FAILURE`，并在设置页面加一个"重新配网"按钮

```c
// main.cpp
.provisioning_mode = WIFI_PROV_ON_FAILURE,
```

设置页面加按钮调用：
```c
wifi_start_provisioning();  // 启动 SoftAP
```

**收益**：
- 换 WiFi 更方便，不用清除数据+重启
- 所有网络都连不上时自动进入配网，更智能

---

### 🟡 中优先级（有空再做）

#### 优化 4：简化 wifi_adapter 层

当前 `wifi_adapter.c` 有 90 多行，但大部分是简单转发。可以：

1. 移除重复函数（`wifi_get_curr_ssid` 等）
2. 移除从未使用的 `s_last_reason` 和 `wifi_reason_str`
3. 扫描缓存移到桥接层，扫描完成事件自动更新缓存
4. 文件可以从 90 行缩减到 30 行以内

---

#### 优化 5：启用 HTTP Basic Auth 保护 API

```c
// main.cpp
.http = {
    .api_base_path = "/api/wifi",
    .enable_auth = true,
    .auth_username = "admin",
    .auth_password = "your_secure_password",
},
```

**注意**：webui 自己的端点不受影响，只有 `/api/wifi/*` 需要认证。

---

#### 优化 6：配置重连耗尽策略

```c
// main.cpp
.max_reconnect_attempts = 20,           // 20 次后执行策略
.on_reconnect_exhausted = WIFI_ON_RECONNECT_EXHAUSTED_RESTART,  // 重启设备
```

**收益**：避免无限重试导致的资源浪费，重启后可能恢复。

---

### 🟢 低优先级（锦上添花）

#### 优化 7：使用自定义变量存储 NAS 配置

`esp_wifi_config` 有自定义变量功能（`wifi_cfg_set_var()` / `wifi_cfg_get_var()`），可通过所有配网接口（Web UI、BLE、CLI）读写。

可以考虑把 NAS 配置也放进去，这样用户在配网时就能一起设置好 NAS 参数。

但这个改动较大，且项目已有完善的 `nas_cfg` 体系，**不建议强行迁移**。

---

#### 优化 8：利用多网络故障转移

如果用户有多个 WiFi（比如 2.4G 和 5G 备用，或家里+办公室），可以配置多网络自动故障转移：

```c
.default_networks = (wifi_network_t[]){
    {"Home_5G", "pass1", 10},   // 优先 5G
    {"Home_2.4G", "pass2", 5},  // 备用 2.4G
},
.default_network_count = 2,
```

---

## 五、建议的实施路径

| 阶段 | 内容 | 预估工作量 | 优先级 |
|------|------|-----------|--------|
| Phase 1 | 事件桥接 + 移除轮询 | 低 | 🔴 高 |
| Phase 2 | 统一存储 + 简化 adapter | 中 | 🔴 高 |
| Phase 3 | 配网模式优化 + 手动触发按钮 | 低 | 🔴 高 |
| Phase 4 | HTTP Auth + 重连策略 | 低 | 🟡 中 |
| Phase 5 | 自定义变量 / 多网络 | 中 | 🟢 低 |

---

## 六、总结

**✅ 做得好的地方**：
1. HTTP 服务器共享的方式正确，避免了端口冲突
2. 有清晰的分层（虽然 adapter 层有点冗余）
3. 配网功能完整可用

**⚠️ 主要改进空间**：
1. **两套事件系统** 是最大的架构问题，需要桥接
2. **双重存储** 会导致数据不一致
3. 配网模式对用户不够友好
4. 库的很多功能没利用起来（多网络、变量存储、认证等）
