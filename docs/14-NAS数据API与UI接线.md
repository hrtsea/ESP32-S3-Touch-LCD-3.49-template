# 14. NAS 数据 API 与 UI 接线指南

> 目标读者：后续开发 UI 屏幕的开发者。本文总结 `main/data` 模块对外暴露的 NAS 数据 API，以及 UI 如何订阅/消费这些数据。

## 14.1 数据流总览

```
┌─────────────┐  poll()   ┌────────────────┐  publish_nas_data  ┌────────────────┐  UI_UPDATE  ┌──────────────┐
│ 数据源客户端 │ ───────▶ │ nas_event_loop  │ ─────────────────▶ │  EVENT_NAS_    │ ──────────▶ │ 各 UI 屏幕    │
│ (DataSource)│           │ (定时抓取任务)   │  (同步回调+异步队列) │  DATA_UPDATE   │             │  (接线点)     │
└─────────────┘           └────────────────┘                    └────────────────┘             └──────────────┘
```

- **生产者**：`data_source_poll()` → `nas_event_loop` 定时任务抓取
- **分发**：`event_bus_publish_nas_data(data)` — 同步调用订阅 handler + 异步入队
- **消费者**：UI 层通过 `event_bus_subscribe(EVENT_NAS_DATA_UPDATE, ...)` 注册**同步回调**（推荐），或从共享队列 `event_bus_receive` 消费

> ⚠️ **关键**：`EVENT_NAS_DATA_UPDATE` 必须用 `event_bus_subscribe` 注册同步回调接收。因为 `event_bus_publish_nas_data` 已**同步**扇出 handler，而共享队列可能被其他任务竞争抢占导致事件丢失。

## 14.2 核心数据结构 `NasData`

文件：[nas_data.h](../main/data/nas_data.h)

```c
typedef struct NasData {
    NasSystemInfo      system;              // 系统信息（CPU/内存/温度）
    NasDiskInfo        disks[MAX_DISKS];    // 磁盘数组
    uint8_t            disk_count;          // 实际磁盘数
    uint8_t            disk_slot_count;     // 总盘位
    NasVolumeInfo      volumes[MAX_VOLUMES];// 存储卷
    uint8_t            volume_count;
    NasServiceInfo     services[MAX_SERVICES];// 服务（含 Docker）
    uint8_t            service_count;
    NasNetworkInfo     network;             // 主网口速率
    NasInterfaceInfo   interfaces[MAX_NETWORK_INTERFACES]; // 全部网口
    uint8_t            interface_count;
    uint8_t            active_interface_idx;
    FanStatus          fan;                 // 风扇状态
    uint32_t           last_update_ms;      // 最后更新时间戳
    bool               is_online;           // 数据源是否在线
    bool               has_update;          // 本轮是否有新数据
} NasData;
```

### 14.2.1 子结构字段说明

| 结构 | 字段 | 类型 | 说明 | UI 常见用途 |
|------|------|------|------|------------|
| `NasSystemInfo` | `hostname` | char[32] | 主机名 | 总览/系统详情标题 |
| | `model` | char[32] | 型号 | 系统详情 |
| | `uptime_s` | uint32_t | 开机秒数 | 系统详情（需换算为天/时） |
| | `cpu_pct` | float | CPU 使用率 % | 总览仪表 |
| | `ram_pct` | float | 内存使用率 % | 总览/系统详情 |
| | `disk_pct` | float | 总磁盘使用率 % | 总览 |
| | `ram_total_mb/used_mb/free_mb/cached_mb` | uint32_t | 内存明细 MB | 系统详情 |
| | `swap_total_mb/used_mb` | uint32_t | 交换分区 MB | 系统详情 |
| | `temp_cpu/temp_sys` | int16_t | 温度 °C | 风扇/系统详情 |
| | `cpu_cores[]`/`cpu_core_count` | float/uint8 | 每核使用率 | 多核图 |
| | `load_avg[3]` | float | 1/5/15min 负载 | 系统详情 |
| `NasDiskInfo` | `name` | char[16] | 磁盘名（如 Disk 1） | 存储屏 |
| | `device`/`model_name`/`mount` | char[] | 设备/型号/挂载点 | 磁盘详情 |
| | `disk_type` | char[8] | 类型（SATA/M2） | 存储屏 |
| | `temp` | int16_t | 温度 °C | 存储屏 |
| | `health` | HealthStatus | 健康状态 | 存储屏（颜色映射） |
| | `size_gb`/`used_gb` | uint32_t | 容量 GB | 存储屏 |
| | `used_pct` | uint8_t | 使用率 % | 存储屏进度条 |
| | `read_kbps`/`write_kbps` | uint32_t | IO 速率 KB/s | 磁盘详情 |
| | `online` | bool | 是否在线 | 存储屏（灰显） |
| | `slot_index` | uint8_t | 盘位号 | 存储屏 |
| `NasVolumeInfo` | `name`/`raid`/`status` | char[] | 卷名/RAID/状态 | 卷列表 |
| | `total_gb`/`used_gb`/`used_pct` | uint32/uint8 | 卷容量 | 卷列表 |
| `NasServiceInfo` | `name` | char[32] | 服务名 | 服务列表 |
| | `running`/`is_docker` | bool | 运行状态/是否 Docker | 服务列表（图标） |
| `NasNetworkInfo` | `interface`/`ip` | char[] | 网口/IP | 网络详情 |
| | `rx_bps`/`tx_bps` | uint32_t | 速率 **bps**（注意不是 KB/s） | 网络仪表 |
| `NasInterfaceInfo` | `name`/`ip`/`rx_bps`/`tx_bps`/`active` | - | 各网口详情 | 网络详情卡片 |
| `FanStatus` | `rpm`/`pwm_pct`/`ctrl_temp`/`stall_alarm`/`enabled` | - | 风扇状态 | 风扇详情 |

> ⚠️ **速率单位**：`rx_bps`/`tx_bps` 单位是 **bps（bit/s）**。UI 换算示例：
> ```c
> uint32_t kb_s = data->network.rx_bps / 8000;   // bps → KB/s
> float mb_s  = data->network.rx_bps / 8000000.0f; // bps → MB/s
> ```

## 14.3 公共 API

### 14.3.1 数据源访问（`data_source.h`）

```c
bool data_source_init(const char *nas_type_id);      // 初始化数据源（按类型 id）
bool data_source_connect(void);                       // 建立连接
bool data_source_disconnect(void);
bool data_source_poll(void);                          // 轮询抓取一次
bool data_source_is_connected(void);
const NasData* data_source_get_data(void);            // 获取当前数据快照（只读）
bool data_source_switch(const char *nas_type_id);     // 切换数据源类型
const char* data_source_get_type_name(void);          // 当前类型显示名
const char* data_source_get_conn_icon(void);          // 连接状态图标字符
float data_source_get_rx_speed_mbps(void);            // RX 速率 MB/s
float data_source_get_tx_speed_mbps(void);            // TX 速率 MB/s
```

**类型辅助**：
```c
const NasTypeEntry* /* NAS_TYPES[] */;               // 支持类型表
int /* DATA_TYPE_COUNT */;
NasType nas_type_from_string(const char *id);        // "synology" → NAS_SYNOLOGY
const char* get_display_type_name(const char *id);   // UI 下拉列表用
```

**UI 常用**：`data_source_get_data()` 拉取快照、`data_source_get_type_name()` 显示类型、`data_source_get_conn_icon()` 显示连接状态。

### 14.3.2 抓取调度（`nas_event_loop.h`）

```c
void nas_event_loop_start(void);                      // 启动抓取任务
void nas_event_loop_stop(void);
bool nas_event_loop_is_running(void);
bool nas_event_loop_switch_source(const char *type_id);// 切换数据源

// 抓取周期定时器
void nas_event_loop_timer_init(void);
void nas_event_loop_timer_start(void);
void nas_event_loop_timer_stop(void);
void nas_event_loop_timer_set_interval_ms(uint32_t ms); // 设置抓取间隔
uint32_t nas_event_loop_timer_get_interval_ms(void);
```

### 14.3.3 JSON 序列化（`nas_data_json.h`）

```c
cJSON *nas_data_to_json(const NasData *data);   // 转 JSON（WebUI 用）
bool nas_json_to_data(cJSON *json, NasData *data); // 反序列化
void nas_data_free_json(cJSON *json);
```

## 14.4 事件接口

### 14.4.1 `EVENT_NAS_DATA_UPDATE`（核心数据事件）

```c
void event_bus_publish_nas_data(const NasData *data);  // 发布端（data 模块内部）
```

**订阅方式（UI 必须用同步回调）**：
```c
static void my_nas_handler(const event_t *evt, void *user_data) {
    if (!evt || !evt->data || evt->data_len < sizeof(NasData)) return;
    const NasData *data = (const NasData *)evt->data;
    // ... UI_UPDATE 宏保护下刷新界面 ...
}

// 初始化时注册
event_bus_subscribe(EVENT_NAS_DATA_UPDATE, my_nas_handler, NULL);
```

> **⚠️ 重要**：`EVENT_NAS_DATA_UPDATE` 的 handler 在**发布者线程**（NAS 抓取任务）上下文中被**同步调用**。handler 内操作 LVGL 必须加 LVGL 锁（见 `ui_events.c` 的 `nas_data_sync_handler` 示例，它用 `UI_UPDATE` 宏），且 handler 要**快速返回**（重活委托给 LVGL timer）。

> **说明**：`UI_UPDATE` 宏是 `ui_events.c` 内部的 LVGL 加锁封装（该文件 L28 定义），非全局 API。**新屏幕**在别的文件中应使用自己的加锁方式（如 `lvgl_port_lock()` / `lvgl_port_unlock()`，或参照 `ui_events.c` 的 `UI_UPDATE` 模式定义等价宏）。

### 14.4.2 其他相关事件

| 事件 | 说明 | 载荷 |
|------|------|------|
| `EVENT_WIFI_CONNECTED/DISCONNECTED` | WiFi 状态变化 | 无（用 `wifi_cfg_get_current_ip` 取 IP） |
| `EVENT_CFG_CHANGED` | 配置变更 | `cfg_change_info_t { cfg_field_t field }` |
| `EVENT_DISK_CONFIG_CHANGED` | 磁盘配置变更 | 无 |

## 14.5 UI 接线指南

### 14.5.1 推荐的接入流程（新屏幕）

1. **订阅数据**：屏幕 `init` 时 `event_bus_subscribe(EVENT_NAS_DATA_UPDATE, handler, NULL)`
2. **handler 内**：校验 `evt->data_len >= sizeof(NasData)`，在 **LVGL 锁内**调用屏幕的 `update` 函数（加锁方式见上方 14.4.1 说明）
3. **屏幕销毁时**：`event_bus_unsubscribe(EVENT_NAS_DATA_UPDATE, handler)` 注销；用全局屏幕指针判空（如 `if (ui_Screen_SystemDetail)`）避免访问已销毁屏幕

### 14.5.2 现有 UI 数据分发入口（参考）

`main/ui/ui_events.c` 的 `on_nas_data_update_evt(data)` 是**所有屏幕的中央分发点**，已覆盖：
- `overview_screen_update_*`（总览）
- `storage_screen_update_*`（存储）
- `sdcopy_screen_update_*`（SD 复制）
- `ui_Screen_DiskDetail_update_data`（磁盘详情）
- `ui_Screen_SystemDetail_update_data`（系统详情）
- `netdetail_screen_update_interfaces`（网络详情）

新增屏幕时，优先**复用在 `on_nas_data_update_evt` 中追加分发**，而不是另起订阅，避免多份订阅重复处理。

### 14.5.3 字段 → 屏幕映射速查

| 数据 | 目标屏幕/函数 |
|------|--------------|
| `cpu_pct`/`ram_pct`/`disk_pct` | 总览仪表、系统详情 |
| `disks[]`/`used_pct`/`temp`/`health` | 存储屏、磁盘详情 |
| `network.rx_bps`/`tx_bps` | 总览/存储/网络详情 |
| `interfaces[]` | 网络详情卡片 |
| `temp_cpu`/`temp_sys` | 风扇详情、系统详情 |
| `fan.rpm`/`pwm_pct` | 风扇屏 |
| `services[]` | 服务列表屏 |
| `volumes[]` | 卷列表屏 |

### 14.5.4 数据有效性判断

```c
if (!data->is_online) { /* 数据源离线：显示离线态 */ }
if (!data->has_update) { /* 本轮无新数据，可跳过刷新 */ }
if (data->last_update_ms == 0) { /* 从未成功抓取过 */ }
```

## 14.6 设计约定与注意事项

1. **只读**：`data_source_get_data()` 返回 `const NasData*`，UI **不可修改**数据，只读展示
2. **同步回调 + LVGL 锁**：所有 UI 更新在 `UI_UPDATE` 宏内，防跨线程 LVGL 竞争
3. **盘位/容量**：`disk_slot_count` 是物理盘位（含空位），`disk_count` 是实际在线盘数；容量单位统一 GB
4. **速率单位陷阱**：网络速率是 bps，注意换算
5. **数据源切换**：切换类型用 `nas_event_loop_switch_source()`，不要直接 `data_source_switch()`（会绕过事件循环）

---

相关文件：
- [nas_data.h](../main/data/nas_data.h)
- [data_source.h](../main/data/data_source.h)
- [nas_event_loop.h](../main/data/nas_event_loop.h)
- [nas_data_json.h](../main/data/nas_data_json.h)
- [event_bus.h](../main/utils/event_bus.h)
- UI 分发入口：[ui_events.c](../main/ui/ui_events.c)
