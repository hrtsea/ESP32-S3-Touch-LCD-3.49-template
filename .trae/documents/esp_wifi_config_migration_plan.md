# esp_wifi_config 库迁移计划

## 一、现有WiFi配网架构总结

### 1.1 现有模块

| 模块 | 文件 | 职责 |
|------|------|------|
| WiFi管理器 | `wifi_manager.c/h` | 连接、断开、扫描、自动重连、漫游 |
| 配网状态机 | `wifi_provision.c/h` | SoftAP配网流程管理，4状态机 |
| Captive Portal | `wifi_provision_http.c/h` | HTTP服务器，配网网页 |
| DNS劫持 | `wifi_provision_dns.c/h` | DNS服务器，将域名解析到192.168.4.1 |

### 1.2 现有全局变量

| 变量 | 类型 | 用途 |
|------|------|------|
| `g_wifi_connected` | bool | WiFi连接状态 |
| `g_wifi_curr_ssid` | char[33] | 当前连接的SSID |
| `g_wifi_scan` | wifi_scan_ap_t[] | 扫描结果数组 |
| `g_wifi_scan_n` | uint16_t | 扫描结果数量 |
| `g_wifi_scanning` | bool | 是否正在扫描 |
| `g_wifi_connect_started_ms` | uint32_t | 连接开始时间 |
| `g_wifi_last_reason` | uint8_t | 最后断开原因 |
| `g_wifi_last_rssi` | int8_t | 最后RSSI |

### 1.3 现有API清单

| API | 声明位置 | 调用位置 | 用途 |
|-----|---------|---------|------|
| `wifi_manager_init()` | wifi_manager.h | main.cpp | 初始化WiFi |
| `wifi_connect()` | wifi_manager.h | wifi_provision.c, ui_events.c, ui_Screen_WifiConfig.c | 连接WiFi |
| `wifi_disconnect()` | wifi_manager.h | ui_events.c | 断开WiFi |
| `wifi_start_scan()` | wifi_manager.h | ui_events.c, ui_Screen_WifiConfig.c | 开始扫描 |
| `wifi_is_connected()` | wifi_manager.h | 多处 | 检查连接状态 |
| `wifi_get_curr_ssid()` | wifi_manager.h | 多处 | 获取当前SSID |
| `wifi_has_credentials()` | wifi_manager.h | main.cpp, ui_events.c | 检查凭证 |
| `wifi_get_scan_count()` | wifi_manager.h | ui_Screen_WifiConfig.c | 获取扫描数量 |
| `wifi_get_scan_ap()` | wifi_manager.h | ui_Screen_WifiConfig.c | 获取扫描结果 |
| `wifi_is_scanning()` | wifi_manager.h | ui_Screen_WifiConfig.c | 检查扫描状态 |
| `wifi_get_last_reason()` | wifi_manager.h | - | 获取断开原因 |
| `wifi_get_connect_started_ms()` | wifi_manager.h | - | 获取连接开始时间 |
| `wifi_manager_register_status_cb()` | wifi_manager.h | - | 注册状态回调 |
| `wifi_provision_init()` | wifi_provision.h | main.cpp | 初始化配网 |
| `wifi_provision_start()` | wifi_provision.h | main.cpp, ui_Screen_WifiConfig.c | 启动配网 |
| `wifi_provision_stop()` | wifi_provision.h | - | 停止配网 |
| `wifi_provision_get_state()` | wifi_provision.h | - | 获取配网状态 |
| `wifi_provision_on_config()` | wifi_provision.h | wifi_provision_http.c | 处理配网配置 |

### 1.4 引用WiFi接口的文件清单

| 文件 | 引用的API |
|------|----------|
| `main/main.cpp` | wifi_manager_init, wifi_provision_init, wifi_has_credentials, wifi_provision_start |
| `main/ui/ui_events.c` | wifi_connect, wifi_start_scan, wifi_is_connected, wifi_has_credentials, wifi_get_curr_ssid, wifi_disconnect |
| `main/ui/ui.h` | #include wifi_manager.h, #include wifi_provision.h |
| `main/ui/screens/ui_Screen_WifiConfig.c` | wifi_get_curr_ssid, wifi_is_connected, g_wifi_connected, wifi_get_scan_count, wifi_get_scan_ap, wifi_start_scan |
| `main/ui/screens/ui_Screen_Overview.c` | wifi_get_curr_ssid, wifi_is_connected |
| `main/config/app_cfg.c` | wifi_connect (回调), EVENT_WIFI_CONNECTED事件处理 |
| `main/utils/bg_fetcher.c` | wifi_is_connected |
| `main/data/nas_event_loop.c` | #include wifi_manager.h |
| `main/data/client/api_client.c` | wifi_is_connected (内部实现) |
| `main/data/client/netdata_client.c` | wifi_is_connected (内部实现) |
| `main/data/client/qnap_client.c` | wifi_is_connected (内部实现) |
| `main/data/client/snmp_client.c` | wifi_is_connected (内部实现) |
| `main/data/client/synology_client.c` | wifi_is_connected (内部实现) |
| `main/data/client/truenas_client.c` | wifi_is_connected (内部实现) |

---

## 二、esp_wifi_config 库API映射

### 2.1 核心初始化API

| esp_wifi_config API | 说明 |
|---------------------|------|
| `wifi_cfg_init(&config)` | 初始化WiFi配置，包含网络列表、配网模式等 |
| `wifi_cfg_wait_connected(timeout)` | 阻塞等待连接 |

### 2.2 配置结构体

```c
typedef struct {
    // 默认网络列表（可选）
    const wifi_network_t *default_networks;
    size_t default_network_count;
    
    // 配网模式
    wifi_prov_mode_t provisioning_mode;  // WIFI_PROV_ON_FAILURE, WIFI_PROV_WHEN_UNPROVISIONED, WIFI_PROV_MANUAL
    
    // SoftAP配网配置
    bool enable_ap;                      // 是否启用SoftAP配网
    const char *ap_ssid;                 // AP名称（可选）
    const char *ap_password;             // AP密码（可选）
    
    // 连接成功后行为
    bool stop_provisioning_on_connect;   // 连接成功后停止配网
    
    // 重连策略
    int reconnect_exhaustion_policy;     // WIFI_CFG_RECONNECT_EXHAUSTION_REBOOT / RETRY_INDEFINITELY
    int max_reconnect_attempts;          // 最大重连次数
    
    // SoftAP网络配置
    uint8_t ap_channel;                  // AP信道
    int ap_max_connections;              // 最大连接数
    
    // REST API配置
    bool enable_rest_api;                // 是否启用REST API
    const char *rest_api_username;       // API用户名（可选）
    const char *rest_api_password;       // API密码（可选）
} wifi_cfg_config_t;
```

### 2.3 网络操作API

| esp_wifi_config API | 对应旧API | 说明 |
|---------------------|----------|------|
| `wifi_cfg_connect(ssid, pass, priority)` | `wifi_connect()` | 连接WiFi |
| `wifi_cfg_disconnect()` | `wifi_disconnect()` | 断开WiFi |
| `wifi_cfg_start_scan()` | `wifi_start_scan()` | 开始扫描 |
| `wifi_cfg_is_connected()` | `wifi_is_connected()` | 检查连接状态 |
| `wifi_cfg_get_current_ssid(buf, len)` | `wifi_get_curr_ssid()` | 获取当前SSID |
| `wifi_cfg_get_scan_result_count()` | `wifi_get_scan_count()` | 获取扫描数量 |
| `wifi_cfg_get_scan_result(idx, result)` | `wifi_get_scan_ap()` | 获取扫描结果 |
| `wifi_cfg_is_scanning()` | `wifi_is_scanning()` | 检查扫描状态 |

### 2.4 配网API

| esp_wifi_config API | 对应旧API | 说明 |
|---------------------|----------|------|
| `wifi_cfg_start_provisioning()` | `wifi_provision_start()` | 启动配网 |
| `wifi_cfg_stop_provisioning()` | `wifi_provision_stop()` | 停止配网 |
| `wifi_cfg_is_provisioning()` | `wifi_provision_get_state()` | 检查配网状态 |

### 2.5 事件系统（基于esp_bus）

| esp_bus事件 | 对应旧事件 | 说明 |
|-------------|-----------|------|
| `WIFI_CFG_EVT_CONNECTED` | `EVENT_WIFI_CONNECTED` | WiFi连接成功 |
| `WIFI_CFG_EVT_DISCONNECTED` | `EVENT_WIFI_DISCONNECTED` | WiFi断开 |
| `WIFI_CFG_EVT_GOT_IP` | `EVENT_WIFI_CONNECTED`(带IP) | 获取IP地址 |
| `WIFI_CFG_EVT_SCAN_DONE` | `EVENT_WIFI_SCAN_DONE` | 扫描完成 |
| `WIFI_CFG_EVT_PROV_STARTED` | `EVENT_WIFI_PROVISION_START` | 配网开始 |
| `WIFI_CFG_EVT_PROV_STOPPED` | `EVENT_WIFI_PROVISION_STOP` | 配网停止 |
| `WIFI_CFG_EVT_PROV_CRED_SUCCESS` | `EVENT_WIFI_PROVISION_CONFIG_RECEIVED` | 配网凭证接收成功 |

---

## 三、迁移策略

### 3.1 整体方案：完全替换

**不使用适配器层**，直接在所有引用旧API的文件中替换为esp_wifi_config的API。

理由：
1. 用户明确要求"完全使用esp_wifi_config库的函数"
2. esp_wifi_config提供了完整的功能覆盖
3. 适配器层会增加维护成本

### 3.2 阶段划分

| 阶段 | 目标 | 关键任务 |
|------|------|---------|
| **Phase 1** | 依赖集成 | 添加esp_wifi_config组件依赖 |
| **Phase 2** | 入口文件修改 | 修改main.cpp，使用新API初始化 |
| **Phase 3** | 配置层修改 | 修改app_cfg.c，适配新事件系统 |
| **Phase 4** | UI层修改 | 修改所有UI文件，替换旧API |
| **Phase 5** | 数据层修改 | 修改数据客户端文件 |
| **Phase 6** | 删除旧文件 | 删除wifi_manager和wifi_provision相关文件 |
| **Phase 7** | 编译测试 | 编译验证和功能测试 |

---

## 四、详细实施步骤

### 4.1 Phase 1：依赖集成

**任务1.1：创建 idf_component.yml**

```yaml
dependencies:
  thorrak/esp_wifi_config: "^0.1.0"
```

**任务1.2：更新 CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "main.cpp"
        # ... 其他源文件 ...
        # 移除：wifi_manager.c, wifi_provision.c, wifi_provision_dns.c, wifi_provision_http.c
    INCLUDE_DIRS
        "."
        "ui"
        "drivers"
        "network"
        "config"
        "utils"
        "data"
    REQUIRES
        esp_wifi_config
)
```

---

### 4.2 Phase 2：入口文件修改

**任务2.1：修改 main.cpp**

```cpp
// 移除旧头文件
// #include "wifi_manager.h"
// #include "wifi_provision.h"

// 添加新头文件
#include "esp_wifi_config.h"
#include "esp_bus.h"

static void network_init(void)
{
    // 初始化esp_bus
    esp_bus_init();
    
    // 配置WiFi
    wifi_cfg_config_t cfg = {
        .provisioning_mode = WIFI_PROV_WHEN_UNPROVISIONED,  // 无凭证时自动进入配网
        .stop_provisioning_on_connect = true,                // 连接成功后停止配网
        .enable_ap = true,                                  // 启用SoftAP配网
        .ap_ssid = "NAS-Monitor",                           // AP名称
        .ap_password = "12345678",                          // AP密码
        .enable_rest_api = true,                             // 启用REST API
    };
    
    wifi_cfg_init(&cfg);
    
    // 等待连接（可选，非阻塞）
    // wifi_cfg_wait_connected(30000);
    
    if (webui_start() != ESP_OK) {
        ESP_LOGW(TAG, "webui_start failed");
    }
}
```

---

### 4.3 Phase 3：配置层修改

**任务3.1：修改 app_cfg.c**

1. 移除WiFi凭证暂存机制（由esp_wifi_config管理）
2. 修改事件订阅，监听esp_bus事件
3. 更新WiFi相关回调

```c
// 移除：
// static void on_wifi_connected_evt(const event_t *evt, void *user_data);
// static char s_pending_ssid[33] = {0};
// static char s_pending_pass[65] = {0};
// static bool s_pending_valid = false;

// 修改：
#include "esp_bus.h"
#include "esp_wifi_config.h"

static void on_wifi_connected(const struct esp_bus_event *evt, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "wifi: connected");
    // esp_wifi_config会自动保存凭证，无需手动处理
}

static void on_wifi_disconnected(const struct esp_bus_event *evt, void *user_data)
{
    (void)user_data;
    ESP_LOGW(TAG, "wifi: disconnected");
}

void app_cfg_init(void)
{
    // ... 原有初始化代码 ...
    
    // 订阅esp_bus事件
    esp_bus_subscribe(WIFI_CFG_EVT_CONNECTED, on_wifi_connected, NULL);
    esp_bus_subscribe(WIFI_CFG_EVT_DISCONNECTED, on_wifi_disconnected, NULL);
}
```

**任务3.2：修改 app_cfg.h**

移除WiFi凭证暂存相关声明：
```c
// 移除：
// void app_cfg_wifi_pending_set(const char *ssid, const char *pass);
// bool app_cfg_wifi_pending_is_valid(void);
// void app_cfg_wifi_pending_commit(void);
// void app_cfg_wifi_pending_clear(void);
```

---

### 4.4 Phase 4：UI层修改

**任务4.1：修改 ui.h**

```c
// 移除：
// #include "wifi_manager.h"
// #include "wifi_provision.h"

// 添加：
#include "esp_wifi_config.h"
```

**任务4.2：修改 ui_events.c**

```c
// 移除：
// #include "wifi_manager.h"

// 添加：
#include "esp_wifi_config.h"

void saveWiFiCredential(lv_event_t * e)
{
    // ... 解析SSID和密码 ...
    
    // 替换：wifi_connect(ssid, password);
    wifi_cfg_connect(ssid, password, 10);  // priority=10
}

void scanNetwork(lv_event_t * e)
{
    // 替换：wifi_start_scan();
    wifi_cfg_start_scan();
}

void toggleWiFi(lv_event_t * e)
{
    bool enabled = lv_obj_has_state(ui_Settings_Switch_Wifi, LV_STATE_CHECKED);
    
    if (enabled) {
        // 替换：wifi_is_connected(), wifi_has_credentials(), wifi_get_curr_ssid()
        if (!wifi_cfg_is_connected()) {
            // esp_wifi_config会自动重连，无需手动连接
            // 如果需要手动触发连接，可以使用wifi_cfg_reconnect()
        }
    } else {
        // 替换：wifi_disconnect();
        wifi_cfg_disconnect();
    }
}
```

**任务4.3：修改 ui_Screen_WifiConfig.c**

```c
// 移除对wifi_manager.h的引用
// 添加对esp_wifi_config.h的引用

// 替换所有旧API调用：
// wifi_get_curr_ssid() -> wifi_cfg_get_current_ssid()
// wifi_is_connected() -> wifi_cfg_is_connected()
// g_wifi_connected -> wifi_cfg_is_connected()
// wifi_get_scan_count() -> wifi_cfg_get_scan_result_count()
// wifi_get_scan_ap() -> wifi_cfg_get_scan_result()
// wifi_start_scan() -> wifi_cfg_start_scan()
```

**任务4.4：修改 ui_Screen_Overview.c**

```c
// 替换：
// wifi_get_curr_ssid() -> wifi_cfg_get_current_ssid()
// wifi_is_connected() -> wifi_cfg_is_connected()
```

---

### 4.5 Phase 5：数据层修改

**任务5.1：修改数据客户端文件**

在以下文件中，将内部的`wifi_is_connected()`实现替换为调用esp_wifi_config：

- `main/data/client/api_client.c`
- `main/data/client/netdata_client.c`
- `main/data/client/qnap_client.c`
- `main/data/client/snmp_client.c`
- `main/data/client/synology_client.c`
- `main/data/client/truenas_client.c`

```c
// 移除内部实现：
// static bool wifi_is_connected(void) { ... }

// 添加：
#include "esp_wifi_config.h"

// 直接使用：
// wifi_cfg_is_connected()
```

**任务5.2：修改 bg_fetcher.c**

```c
// 替换：wifi_is_connected() -> wifi_cfg_is_connected()
```

**任务5.3：修改 nas_event_loop.c**

```c
// 移除：#include "wifi_manager.h"
// 如果需要检查WiFi状态，添加：#include "esp_wifi_config.h"
```

---

### 4.6 Phase 6：删除旧文件

删除以下文件：

| 文件 | 原因 |
|------|------|
| `main/network/wifi_manager.c` | 功能由esp_wifi_config替代 |
| `main/network/wifi_manager.h` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision.c` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision.h` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_http.c` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_http.h` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_dns.c` | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_dns.h` | 功能由esp_wifi_config替代 |

---

### 4.7 Phase 7：编译测试

**任务7.1：清理构建缓存**

```bash
rm -rf build
```

**任务7.2：编译项目**

```bash
& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py build
```

**任务7.3：功能验证清单**

- [ ] 首次启动自动进入配网模式（SoftAP）
- [ ] Captive Portal网页可访问
- [ ] WiFi配置成功后自动连接
- [ ] 连接成功后凭证正确保存
- [ ] 重启后自动重连到上次网络
- [ ] WiFi扫描功能正常（UI显示可用网络）
- [ ] UI状态正确更新（连接/断开/配网中）
- [ ] 配网超时机制正常（5分钟后重启）
- [ ] NAS数据获取正常（依赖WiFi连接）

---

## 五、风险与注意事项

### 5.1 风险点

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| **NVS数据迁移** | 旧的WiFi凭证格式与新库不兼容 | 在首次启动时检测并迁移旧格式数据 |
| **事件系统冲突** | 项目使用自定义event_bus，esp_wifi_config使用esp_bus | 在app_cfg.c中订阅esp_bus事件并转发到自定义event_bus |
| **WebUI端口冲突** | esp_wifi_config的Captive Portal和项目webui可能端口冲突 | 配置esp_wifi_config使用不同端口或禁用内置Web UI |
| **编译错误** | API不兼容导致编译失败 | 逐步迁移，每次编译验证 |
| **运行时错误** | 事件处理逻辑不一致 | 添加详细日志，逐步测试 |

### 5.2 注意事项

1. **事件转发**：需要将esp_bus事件转发到项目的自定义event_bus，以保持UI层兼容性
2. **NVS数据迁移**：首次启动时检测旧格式凭证并迁移到esp_wifi_config的存储格式
3. **测试顺序**：先编译通过，再测试配网功能，最后测试自动重连和业务功能
4. **日志保留**：保留原有WiFi相关日志格式，便于问题排查

---

## 六、文件修改清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `idf_component.yml` | 新建 | 添加esp_wifi_config依赖 |
| `main/CMakeLists.txt` | 修改 | 添加REQUIRES esp_wifi_config，移除旧源文件 |
| `main/main.cpp` | 修改 | 使用esp_wifi_config初始化 |
| `main/config/app_cfg.c` | 修改 | 订阅esp_bus事件，移除凭证暂存机制 |
| `main/config/app_cfg.h` | 修改 | 移除凭证暂存相关声明 |
| `main/ui/ui.h` | 修改 | 替换头文件引用 |
| `main/ui/ui_events.c` | 修改 | 替换WiFi API调用 |
| `main/ui/screens/ui_Screen_WifiConfig.c` | 修改 | 替换WiFi API调用 |
| `main/ui/screens/ui_Screen_Overview.c` | 修改 | 替换WiFi API调用 |
| `main/utils/bg_fetcher.c` | 修改 | 替换WiFi API调用 |
| `main/data/nas_event_loop.c` | 修改 | 移除旧头文件引用 |
| `main/data/client/api_client.c` | 修改 | 替换WiFi API调用 |
| `main/data/client/netdata_client.c` | 修改 | 替换WiFi API调用 |
| `main/data/client/qnap_client.c` | 修改 | 替换WiFi API调用 |
| `main/data/client/snmp_client.c` | 修改 | 替换WiFi API调用 |
| `main/data/client/synology_client.c` | 修改 | 替换WiFi API调用 |
| `main/data/client/truenas_client.c` | 修改 | 替换WiFi API调用 |
| `main/network/wifi_manager.c` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_manager.h` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision.c` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision.h` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_http.c` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_http.h` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_dns.c` | 删除 | 功能由esp_wifi_config替代 |
| `main/network/wifi_provision_dns.h` | 删除 | 功能由esp_wifi_config替代 |

---

## 七、验证标准

- [ ] 编译成功，无错误和警告
- [ ] 首次启动自动进入配网模式
- [ ] SoftAP正常启动，可连接（SSID: NAS-Monitor）
- [ ] Captive Portal网页可访问（http://192.168.4.1）
- [ ] WiFi配置成功后自动连接
- [ ] 连接成功后凭证正确保存（重启后自动重连）
- [ ] WiFi扫描功能正常（UI显示可用网络列表）
- [ ] UI状态正确更新（连接/断开/配网中）
- [ ] 配网超时机制正常（5分钟后重启配网）
- [ ] NAS数据获取正常（WiFi连接后数据更新）
- [ ] 所有数据客户端正常工作
