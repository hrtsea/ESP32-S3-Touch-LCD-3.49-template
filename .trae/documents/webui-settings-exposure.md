# 将全部配置项暴露给 WebUI 的实施计划

## Summary（摘要）

在现有 `components/webui/webui.c` 基础上，新增 `GET /api/settings` 与 `POST /api/settings` 两个端点，把 `AppConfig` 结构体中全部 ~25 个配置字段（WiFi/NAS/显示/磁盘/风扇/时区/天气/自动轮播）一次性暴露给 WebUI，并在嵌入式 HTML 单页应用中新增 "Device Settings" 区块，让用户从浏览器即可读写所有设备配置。配置变更后持久化到 NVS，能热应用的立即生效，不能热应用的提示重启生效。

## Current State Analysis（现状分析）

### 已有 WebUI 基础设施
- **文件**：[components/webui/webui.c](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/components/webui/webui.c)（单文件，~1200 行）
- **HTTP 服务器**：端口 80，`httpd_start()` 在 `webui_start()` 启动，`max_uri_handlers=24`（当前用了 17 个，剩余 7 个槽位）
- **HTML 前端**：`k_index_html` 嵌入式单页应用（L211-518），含 CSS + JS
- **现有端点模式**：
  - `GET` 返回 JSON：`h_state` (L535) 用 `snprintf` 拼 JSON
  - `POST` 接收 form-urlencoded：`h_cfg` (L664) 用 `read_body()` + `form_int()` + `strstr()` 解析
  - 响应：`send_str(r, "application/json", "{\"ok\":true}")`
- **Auth**：`CHECK_AUTH(req)` 宏 (L114)，Basic Auth 可选

### 已暴露的配置（通过 app_cfg_get/set 间接）
brightness, dim_s, off_s, clock 系列, background 系列, quotes 系列 —— 这些走 `app_cfg_xxx` 包装层，不走 `g_config`。

### 未暴露的 config 字段（本次要加的）
来自 [config.h:94-124](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/config/config.h#L94-L124) `AppConfig`：

| 分组 | 字段 | 类型 | NVS 键 | setter API | 热应用? |
|---|---|---|---|---|---|
| WiFi | ssid | char[33] | wifi_ssid | config_save_wifi | ❌ 需重连 |
| WiFi | wifipass | char[65] | wifi_pass | config_save_wifi | ❌ 需重连 |
| NAS | nas_type | char[16] | nas_type | config_save_nas | ❌ 需重启数据源 |
| NAS | nas_ip | char[40] | nas_ip | config_save_nas | ❌ |
| NAS | nas_port | uint16_t | nas_port | config_save_nas | ❌ |
| NAS | nas_user | char[32] | nas_user | config_save_nas | ❌ |
| NAS | nas_pass | char[65] | nas_pass | config_save_nas | ❌ |
| NAS | nas_https | bool | nas_https | config_save_nas | ❌ |
| NAS | snmp_comm | char[32] | snmp_comm | config_save_nas | ❌ |
| NAS | snmp_ver | uint8_t | snmp_ver | config_save_nas | ❌ |
| NAS | serial_baud | uint32_t | serial_baud | config_save_nas | ❌ |
| 显示 | poll_sec | uint8_t | poll_sec | config_save_display | ✅ |
| 显示 | rotation_angle | uint8_t | rotation_angle | config_save_display | ❌ 需重启 |
| 显示 | autodim | bool | autodim | config_save_display | ✅ |
| 时区 | timezone | int8_t | timezone | 无（直接写 g_config + nvs） | ✅ |
| 自动轮播 | auto_cycle_enabled | bool | auto_cycle_en | 无 | ✅ |
| 自动轮播 | auto_cycle_interval_sec | uint8_t | auto_cycle_int | 无 | ✅ |
| 磁盘 | sata_disk_count | uint8_t | sata_count | config_save_disk_config | ❌ 需重启 |
| 磁盘 | m2_disk_count | uint8_t | m2_count | config_save_disk_config | ❌ 需重启 |
| 天气 | weather_api_key | char[65] | weather_key | 无 | ✅ |
| 天气 | weather_city | char[32] | weather_city | 无 | ✅ |
| 风扇 | fan.* | FanConfig | fan_* | config_save_fan | ✅ |

### 现有 setter 签名
- `config_save_wifi(const char* ssid, const char* pass)`
- `config_save_nas(const char* type, const char* ip, uint16_t port, const char* user, const char* pass, bool https)`
- `config_save_display(uint8_t rotation_angle, uint8_t brightness, bool autodim)`
- `config_save_fan(const FanConfig* fan)`
- `config_save_disk_config(uint8_t sata_count, uint8_t m2_count)`

**FanConfig 实际字段**（来自 [main/data/fan_control.h:57-69](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/fan_control.h#L57-L69)）：
```c
typedef struct FanConfig {
    FanCurvePoint curve[FAN_CURVE_POINTS];  // 5 个 {int16_t temp, uint8_t pwm_pct}
    TempSource temp_source;   // enum: 0=MAX_CPU_SYS, 1=AVG_CPU_SYS, 2=CPU_ONLY, 3=SYS_ONLY
    uint8_t hysteresis;
    uint8_t min_change_pct;
    uint8_t min_pwm_pct;
    int16_t emergency_temp;
    uint8_t stall_detect_sec;
    uint16_t ramp_time_ms;
    FanMode mode;             // enum: 0=AUTO, 1=MANUAL
    uint8_t manual_pwm_pct;
    bool enabled;
} FanConfig;
```

**关键架构约束**：`components/webui/CMakeLists.txt` 的 REQUIRES **不包含 main**（避免循环依赖，因为 main REQUIRES webui）。现有 webui.c 通过 `extern void app_cfg_xxx()` 声明访问 main 符号。要访问 `g_config` 和 `config_save_*`，需在 CMakeLists 的 INCLUDE_DIRS 中添加 main 路径 + extern 声明，而非 REQUIRES main。

**问题**：`config_save_nas` 不包含 `snmp_comm/snmp_ver/serial_baud`，这三个字段需要单独持久化。`timezone/auto_cycle/weather_*` 没有专门 setter，需要直接写 `g_config` + `nvs_set_*`。

## Proposed Changes（变更方案）

### 决策
1. **不新增文件**，全部在 `components/webui/webui.c` 内扩展（遵循"简洁优先"原则）
2. **新增 2 个端点**：`GET /api/settings`（读全部配置）、`POST /api/settings`（写配置）
3. **HTML 前端新增 "Device Settings" section**，按分组（WiFi/NAS/Display/Disk/Fan/Time/Weather/Cycle）组织表单
4. **持久化策略**：POST 时调用对应 `config_save_xxx()` 写 NVS；缺少 setter 的字段（timezone/auto_cycle/weather/snmp_extra）在 webui.c 内用 `nvs_open`+`nvs_set_*` 直接写，同步更新 `g_config`
5. **热应用策略**：能热应用的字段 POST 后立即调用应用函数（如 `app_cfg_set_brightness`）；不能热应用的在响应 JSON 中返回 `reboot_required: true`
6. **安全**：WiFi 密码/NAS 密码在 GET 时返回空字符串（不回显敏感信息），POST 时若字段为空则保留原值
7. **不引入新依赖**：复用现有 `read_body`/`form_int`/`send_str`/`CHECK_AUTH` 工具函数

### 文件 1: [components/webui/webui.c](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/components/webui/webui.c)

#### 变更 1.1: 顶部增加 include 与 extern 声明（L36 附近）
```c
#include "config.h"          // g_config, AppConfig, config_save_*（路径通过 CMakeLists INCLUDE_DIRS 暴露）
#include "fan_control.h"     // FanConfig, FanMode, TempSource（在 main/data/）
#include "nvs_flash.h"       // nvs_open/nvs_set_* for fields without setter
```
无需额外 extern 声明 —— config.h 已声明 `extern AppConfig g_config;` 和所有 `config_save_*` 函数，include 即可。符号链接靠链接器解析 main 组件的全局符号（与现有 `extern void app_cfg_xxx` 同机制）。

#### 变更 1.2: 新增辅助函数（L204 `read_body` 之后）
- `form_str(const char *body, const char *key, char *out, size_t out_sz)` — 从 form-urlencoded 提取字符串字段（URL-decode `+` 和 `%xx`）
- `form_bool(const char *body, const char *key, bool def)` — 解析 `key=0/1/on/true`
- `settings_persist_misc(const AppConfig *src)` — 将 timezone/auto_cycle/weather/snmp_comm/snmp_ver/serial_baud 直接写 NVS（这些没有专门 setter）

#### 变更 1.3: 新增 `h_settings_get` handler（L626 `h_list` 之后）
```c
static esp_err_t h_settings_get(httpd_req_t *r)
```
- `CHECK_AUTH(r)`
- 读取 `g_config` 全部字段
- 敏感字段（wifipass/nas_pass）返回空字符串
- 拼接 JSON（~1024 字节），`send_str(r, "application/json", json)`

JSON 结构：
```json
{
  "wifi_ssid":"", "wifi_pass":"",
  "nas_type":"mock", "nas_ip":"", "nas_port":0,
  "nas_user":"", "nas_pass":"", "nas_https":0,
  "snmp_comm":"public", "snmp_ver":2, "serial_baud":115200,
  "poll_sec":5, "rotation_angle":0, "autodim":1,
  "timezone":8,
  "auto_cycle_enabled":0, "auto_cycle_interval_sec":10,
  "sata_disk_count":6, "m2_disk_count":3,
  "weather_api_key":"", "weather_city":"",
  "fan_enabled":1, "fan_mode":0, "fan_manual_pct":50,
  "fan_temp_source":0, "fan_hysteresis":3, "fan_min_change_pct":5,
  "fan_min_pwm_pct":20, "fan_emergency_temp":75,
  "fan_stall_detect_sec":10, "fan_ramp_time_ms":500,
  "fan_curve":[{"temp":25,"pct":25},{"temp":35,"pct":30},{"temp":45,"pct":50},{"temp":55,"pct":80},{"temp":65,"pct":100}]
}
```

#### 变更 1.4: 新增 `h_settings_post` handler（紧随 1.3）
```c
static esp_err_t h_settings_post(httpd_req_t *r)
```
- `CHECK_AUTH(r)`
- `read_body`（buffer 扩大到 2048，因字段多）
- 按 form key 分组调用 `config_save_*`：
  - `wifi_ssid` 或 `wifi_pass` 存在 → `config_save_wifi(ssid, pass)`（空值保留原 `g_config` 值）
  - `nas_type/nas_ip/nas_port/nas_user/nas_pass/nas_https` 任一存在 → `config_save_nas(...)`（缺的字段从 `g_config` 取）
  - `snmp_comm/snmp_ver/serial_baud` → 直接写 `g_config` + `settings_persist_misc`
  - `poll_sec/rotation_angle/autodim` → `config_save_display(rotation, brightness, autodim)`（brightness 从 `g_config` 取保持不变）
  - `sata_disk_count/m2_disk_count` → `config_save_disk_config`
  - `timezone/auto_cycle_*/weather_*` → 直接写 `g_config` + `settings_persist_misc`
  - `fan_*` → 组装 `FanConfig` + `config_save_fan`
- 热应用：brightness 已有 `/api/cfg` 覆盖；autodim/timezone/fan 可通过事件总线或直接调用应用函数（若有）
- 响应：`{"ok":true,"reboot_required":<0|1>}`，当修改了 nas_*/rotation/disk_count/wifi 时 `reboot_required=1`

#### 变更 1.5: 注册路由（L1138 `routes[]` 数组）
在数组末尾、`/rec/*` 之前插入：
```c
{ .uri = "/api/settings", .method = HTTP_GET,  .handler = h_settings_get },
{ .uri = "/api/settings", .method = HTTP_POST, .handler = h_settings_post },
```
`max_uri_handlers` 从 24 调到 28（L1185），容纳新增 2 个。

#### 变更 1.6: HTML 前端新增 "Device Settings" section（L280 `</section>` 之后，`<section><h2>Quotes` 之前）
新增一个 `<section id=settings>`，包含按分组的表单控件：
- **WiFi**: ssid(text), pass(password)
- **NAS**: type(select: mock/synology/qnap/truenas/unraid/netdata/fnos/terrarmaster/linux_http/linux_serial/snmp), ip(text), port(number), user(text), pass(password), https(checkbox), snmp_comm(text), snmp_ver(select 1/2/3), serial_baud(number)
- **Display**: poll_sec(number 1-30), rotation_angle(select 0/90/180/270), autodim(checkbox)
- **Disk**: sata_disk_count(number 0-16), m2_disk_count(number 0-16)
- **Time**: timezone(number -12~14), auto_cycle_enabled(checkbox), auto_cycle_interval_sec(number 1-300)
- **Weather**: weather_api_key(text), weather_city(text)
- **Fan**: enabled(checkbox), mode(select auto/manual), manual_pct(range 0-100), temp_source(select 0-3), hysteresis(number), min_change_pct(number), min_pwm_pct(range 0-100), emergency_temp(number), stall_detect_sec(number), ramp_time_ms(number), curve(5 组 temp+pct 输入框)
- 保存按钮：`fetch('/api/settings',{method:'POST',body:new FormData(form)})`，响应中若 `reboot_required` 显示提示

JS 逻辑（在现有 `<script>` 内追加）：
- 页面加载时 `fetch('/api/settings').then(r=>r.json()).then(fillForm)` 填充表单
- 保存按钮 `onclick` 提交 form，显示 `reboot_required` 提示

### 文件 2: [components/webui/CMakeLists.txt](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/components/webui/CMakeLists.txt)
当前内容（L1-5）：
```cmake
idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server fatfs sdcard_bsp recorder radio
             esp_wifi esp_netif lvgl mbedtls)
```
**变更**：在 `INCLUDE_DIRS` 中添加 main 源码路径，使 webui.c 能 `#include "config.h"` 和 `#include "fan_control.h"`（不加入 REQUIRES main，避免循环依赖）：
```cmake
idf_component_register(
    SRCS "webui.c"
    INCLUDE_DIRS "." "../../main" "../../main/data" "../../main/config"
    REQUIRES esp_http_server fatfs sdcard_bsp recorder radio
             esp_wifi esp_netif lvgl mbedtls nvs_flash)
```
- 新增 `nvs_flash` 到 REQUIRES（webui.c 需要直接调用 `nvs_open`/`nvs_set_*` 持久化无 setter 的字段）
- 符号链接（`g_config`、`config_save_*`）靠链接器解析 main 组件全局符号，与现有 `extern void app_cfg_xxx` 同机制

### 文件 3: [main/CMakeLists.txt](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/CMakeLists.txt)
- 确认 `webui` 在 PRIV_REQUIRES 或 REQUIRES 中（使 main 能调用 webui_start）—— 已存在，无需改

## Assumptions & Decisions（假设与决策）

1. **假设**：`config.h` 的 `g_config` 是全局可访问的（已确认 `extern AppConfig g_config` 在 config.h:139）
2. **假设**：`FanConfig` 结构体定义在 `fan_control.h`（config.h:5 include 了它）—— 实现时需读取确认字段名
3. **决策**：敏感字段（密码）GET 时不回显，POST 时空值保留原值 —— 避免 WebUI 泄露密码
4. **决策**：不新增事件总线事件，热应用通过直接调用应用函数（如 autodim 直接写 `g_config.autodim` 即生效，因为 disp_driver 每次读 g_config）
5. **决策**：不实现"立即重启"按钮，只提示 `reboot_required`，由用户手动重启 —— 避免误操作
6. **决策**：NAS 类型下拉框选项与 `ui_Screen_Settings_NasTab.c` 保持一致（实现时需读取该文件确认完整列表）
7. **决策**：风扇曲线 `fan_curve` 是 5 个 `FanCurvePoint{temp, pwm_pct}`，POST 时用 `fan_curve_0_temp/fan_curve_0_pct/.../fan_curve_4_temp/fan_curve_4_pct` 共 10 个 key 传输，组装进 `FanConfig.curve[]` 后调用 `config_save_fan`
8. **决策**：FanMode 只有 AUTO(0)/MANUAL(1) 两个枚举值，无 curve 模式（曲线在 AUTO 模式下生效，通过 `curve[]` 配置）

## Verification Steps（验证步骤）

1. **编译验证**：`& "C:\esp\v5.5.4\esp-idf\export.ps1"; idf.py build` 通过，无 warning/error
2. **API 读验证**：设备启动后 `curl http://<device_ip>/api/settings` 返回完整 JSON，包含全部 25+ 字段
3. **API 写验证**：`curl -X POST -d "nas_type=synology&nas_ip=192.168.1.100&nas_port=5000" http://<device_ip>/api/settings` 返回 `{"ok":true,"reboot_required":1}`，重启后 `g_config.nas_type=="synology"`
4. **敏感字段验证**：GET 响应中 `wifi_pass` 和 `nas_pass` 为空字符串
5. **前端验证**：浏览器打开 `http://<device_ip>/ui`，滚动到 "Device Settings" section，表单字段被正确填充；修改 brightness 保存后设备屏幕亮度立即变化；修改 nas_type 保存后显示 "reboot required" 提示
6. **NVS 持久化验证**：修改配置 → 重启 → `curl /api/settings` 确认值保留
7. **Auth 验证**（若启用）：未认证请求返回 401
8. **边界验证**：sata_disk_count=0、m2_disk_count=0 时不崩溃；timezone=-12~14 范围外被钳制
