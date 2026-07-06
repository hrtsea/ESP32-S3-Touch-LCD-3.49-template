# ESP32-S3 NAS监控显示屏 —— 架构迁移计划

## 文档版本
V2.0

## 一、总体架构原则

1. **事件总线是模块间唯一通信通道**，禁止模块之间直接函数调用
2. **生产者-消费者模型**：生产者推送事件到总线，UI任务唯一消费者
3. **所有业务任务常态阻塞在消息队列**，禁止使用 vTaskDelay() 作为业务轮询延时
4. **多数据源归一化**：所有客户端输出统一 NasData 结构体，转换为同格式 JSON 推送事件
5. **内存契约**：cJSON 对象由生产者分配，消费方释放

## 二、四层分层架构

```
┌─────────────────────────────────────────────────────┐
│ 4. UI表现层（lvgl_ui）【事件消费者】                 │
│    订阅 EVENT_NAS_DATA_UPDATE → 更新屏幕           │
└───────────────┬─────────────────────────────────────┘
                │ ↑↓ 事件总线
┌─────────────────────────────────────────────────────┐
│ 3. 消息调度层（event_bus）                          │
│    FreeRTOS Queue + 发布/订阅混合模式               │
└───────────────┬─────────────────────────────────────┘
                │ ↑↓
┌─────────────────────────────────────────────────────┐
│ 2. 数据源生产者层（data_source）【抽象工厂模式】     │
│    ┌─────────────────────────────────────────────┐  │
│    │ 工厂 (data_source.c)                       │  │
│    │  ├── data_source_create(type_id)           │  │
│    │  ├── data_source_init(type_id)             │  │
│    │  ├── data_source_connect()                 │  │
│    │  ├── data_source_poll()                    │  │
│    │  └── data_source_switch(type_id)           │  │
│    └───────────────┬─────────────────────────────┘  │
│                    │                                │
│    ┌───────────────┼───────────────┐               │
│    ▼               ▼               ▼               │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │ Client  │  │ Client  │  │ Client  │            │
│  │ (VTable)│  │ (VTable)│  │ (VTable)│            │
│  │ synology│  │ serial  │  │ mock    │            │
│  │ qnap    │  │ api     │  │ netdata │            │
│  │ truenas │  │ snmp    │  │ ...     │            │
│  └─────────┘  └─────────┘  └─────────┘            │
└───────────────┬─────────────────────────────────────┘
                │
┌─────────────────────────────────────────────────────┐
│ 1. 硬件驱动层                                       │
│    UART Driver / WiFi / HTTP Client / SPI LCD      │
└─────────────────────────────────────────────────────┘
```

## 三、抽象工厂模式实现

### 3.1 抽象接口定义 (data_source.h)

```c
typedef struct DataSourceVTable {
    bool (*init)(DataSource* self);
    bool (*connect)(DataSource* self);
    void (*disconnect)(DataSource* self);
    bool (*poll)(DataSource* self);
    bool (*is_connected)(DataSource* self);
    const NasData* (*get_data)(DataSource* self);
    const char* (*get_type_name)(DataSource* self);
    const char* (*get_conn_icon)(DataSource* self);
    NasTypeConfig (*get_config)(DataSource* self);
    void (*destroy)(DataSource* self);
} DataSourceVTable;

struct DataSource {
    const DataSourceVTable* vtable;
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    void* priv;
};
```

### 3.2 工厂函数 (data_source.c)

| 函数 | 功能 |
|------|------|
| `data_source_create(type_id)` | 根据类型ID创建对应数据源实例 |
| `data_source_init(type_id)` | 初始化全局数据源 |
| `data_source_connect()` | 连接数据源 |
| `data_source_poll()` | 轮询获取数据 |
| `data_source_switch(type_id)` | 切换数据源类型 |
| `data_source_get_data()` | 获取当前数据 |

### 3.3 支持的NAS类型

| 类型ID | 显示名称 | 枚举 | 协议 |
|--------|---------|------|------|
| synology | Synology DSM | NAS_SYNOLOGY | HTTP API |
| qnap | QNAP QTS | NAS_QNAP | HTTP API |
| truenas | TrueNAS | NAS_TRUENAS | HTTP API |
| linux_http | Linux (HTTP) | NET_LINUX_HTTP | HTTP API |
| linux_serial | Linux (Serial) | NET_LINUX_SERIAL | UART |
| netdata | Netdata | NET_NETDATA | HTTP API |
| snmp | SNMP | NET_SNMP | SNMP |
| windows | Windows | NET_WINDOWS | HTTP API |
| mock | Mock (测试) | NAS_MOCK | 模拟 |

## 四、事件契约

```c
typedef enum {
    EVENT_NAS_DATA_UPDATE,    // NAS状态JSON数据更新 (data=cJSON*)
    EVENT_TRIGGER_HTTP_FETCH, // 触发一次数据采集 (data=NULL)
    EVENT_HTTP_STOP,          // 暂停数据采集 (data=NULL)
    EVENT_WIFI_CONNECTED,     // WiFi连接成功
    EVENT_WIFI_DISCONNECTED   // WiFi断开
} event_id_t;
```

## 五、任务清单

### 任务1：task_nas_data_loop（数据源任务，优先级1）
- 阻塞等待事件总线：`event_bus_receive(&evt, portMAX_DELAY)`
- 收到 `EVENT_TRIGGER_HTTP_FETCH`：调用 `data_source_poll()` → 获取 `NasData` → 转换为 cJSON → 推送 `EVENT_NAS_DATA_UPDATE`
- 收到 `EVENT_WIFI_CONNECTED/DISCONNECTED`：控制采集开关

### 任务2：task_lvgl_event_loop（UI任务，优先级2）
- 订阅 `EVENT_NAS_DATA_UPDATE` 事件
- 收到事件：解析 cJSON → 更新屏幕 → `cJSON_Delete()`

### 系统定时器（FreeRTOS Timer）
- 周期2000ms → 推送 `EVENT_TRIGGER_HTTP_FETCH`

## 六、数据流设计

### 数据流1：HTTP采集（事件驱动）

```
FreeRTOS Timer → EVENT_TRIGGER_HTTP_FETCH → task_nas_data_loop
    → data_source_poll() → 具体Client采集 → NasData
    → nas_data_to_json() → cJSON
    → event_bus_publish(EVENT_NAS_DATA_UPDATE) → UI任务 → 更新屏幕
```

### 数据流2：串口采集（推送型）

```
NAS UART → serial_client后台任务 → NasData缓存
    → data_source_poll() 返回缓存数据
    → nas_data_to_json() → cJSON
    → event_bus_publish(EVENT_NAS_DATA_UPDATE) → UI任务 → 更新屏幕
```

### 数据流3：手动刷新

```
LVGL按钮点击 → event_bus_publish(EVENT_TRIGGER_HTTP_FETCH) → ...
```

## 七、数据转换层 (nas_data_json.c)

| 函数 | 功能 |
|------|------|
| `nas_data_to_json(data)` | NasData → cJSON（生产者使用） |
| `nas_json_to_data(json, data)` | cJSON → NasData（反向解析） |
| `nas_data_free_json(json)` | 释放cJSON对象 |

## 八、架构优势

1. **开闭原则**：新增NAS厂商只需实现 VTable 接口，工厂新增枚举分支，不修改上层代码
2. **统一数据标准**：所有采集源归一输出 NasData，UI层无感知底层协议
3. **可快速切换数据源**：修改工厂创建时的 type_id 即可切换品牌/采集方式
4. **事件驱动完全兼容**：原有事件总线、定时器、按钮逻辑全部保留
5. **单元测试友好**：mock_client 实现模拟数据，无需真实NAS硬件

## 九、扩展场景预留

- **双数据源同时启用**：创建两个工厂实例，两路独立推送相同事件
- **动态切换NAS类型**：UI发送切换事件，任务销毁旧实例、重建新数据源
- **多NAS轮询**：工厂支持创建多个数据源句柄，JSON中增加设备标识

## 十、文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `main/data/data_source.h` | 保留 | 抽象接口定义 |
| `main/data/data_source.c` | 保留 | 工厂实现 |
| `main/data/nas_data_json.h` | 新建 | NasData-JSON转换头文件 |
| `main/data/nas_data_json.c` | 新建 | NasData-JSON转换实现 |
| `main/data/nas_event_loop.h` | 新建 | 数据源事件循环头文件 |
| `main/data/nas_event_loop.c` | 新建 | 事件驱动数据源任务 |
| `main/utils/event_bus.h` | 修改 | 新增NAS事件类型 + receive函数 |
| `main/utils/event_bus.c` | 修改 | 添加事件队列 |
| `main/utils/http_timer.h` | 新建 | HTTP定时器头文件 |
| `main/utils/http_timer.c` | 新建 | HTTP定时器实现 |
| `main/ui/ui_events.c` | 修改 | 订阅EVENT_NAS_DATA_UPDATE |
| `main/main.cpp` | 修改 | 集成各模块 |
| `main/CMakeLists.txt` | 修改 | 添加新文件到编译 |
| `main/data/client/*.c/h` | 保留 | 各厂商客户端实现 |
