# UI 界面与 data_source 对接计划

## 一、现状分析

### 1.1 data_source 模块能力

[`data_source.c`](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/data_source.c) 提供了完整的 NAS 数据采集接口：

| API | 功能 | 当前使用情况 |
|-----|------|-------------|
| `data_source_get_data()` | 获取 NAS 完整数据（CPU/内存/磁盘/网络） | Overview 部分使用 |
| `data_source_is_connected()` | 检查数据源连接状态 | 未使用 |
| `data_source_get_rx_speed_mbps()` | 获取下载速度 | Overview 使用 |
| `data_source_get_tx_speed_mbps()` | 获取上传速度 | Overview 使用 |
| `data_source_switch()` | 切换数据源类型 | 设置保存后未调用 |
| `data_source_poll()` | 轮询刷新数据 | 未在 UI 定时器中调用 |

### 1.2 UI 界面现状

| 界面 | 当前数据源 | 问题 |
|------|-----------|------|
| `ui_Screen_Overview.c` | 部分对接 `data_source_get_data()` | 缺少连接状态显示、HDD 指示灯未对接 |
| `ui_Screen_Storage.c` | 硬编码数据（`hdd_names`、`hdd_usage`、`hdd_active`） | 完全未对接真实数据 |
| `ui_Screen_Settings_NasTab.c` | 使用 `NAS_TYPES` 枚举 | 保存配置后未调用 `data_source_switch()` |

### 1.3 数据模型

[`nas_data.h`](file:///e:/zotlabnas-split/ESP32-S3-Touch-LCD-3.49-template/main/data/nas_data.h) 定义的数据结构：

```c
typedef struct NasData {
    NasSystemInfo   system;      // CPU/内存/温度/负载
    NasDiskInfo     disks[MAX_DISKS];   // 磁盘列表
    uint8_t         disk_count;
    NasVolumeInfo   volumes[MAX_VOLUMES]; // 卷列表
    uint8_t         volume_count;
    NasNetworkInfo  network;     // 网络速度
    bool            is_online;   // 在线状态
} NasData;
```

## 二、实施计划

### 任务 1：完善 Overview 界面数据对接

**目标**：将 Overview 界面的所有数据展示与 `data_source` 完整对接

**文件**：`main/ui/screens/ui_Screen_Overview.c`

**修改内容**：

1. **添加头文件引用**：
   - 添加 `#include "data_source.h"`（如果未包含）

2. **完善 update_timer_cb 函数**：
   - 添加连接状态判断，离线时显示灰色/警告
   - 添加 HDD 指示灯状态更新（对接 `data->disks`）
   - 添加系统信息（hostname、model、uptime）显示

3. **添加连接状态图标**：
   - 添加 NAS 连接状态图标（绿色=在线，红色=离线）

4. **修复 IP 显示**：
   - 当前显示 `g_config.nas_ip`（配置的 NAS IP），应显示设备自身 IP

### 任务 2：Storage 界面数据对接

**目标**：将 Storage 界面的磁盘数据从硬编码改为从 `data_source` 获取

**文件**：`main/ui/screens/ui_Screen_Storage.c`

**修改内容**：

1. **添加头文件引用**：
   - 添加 `#include "data_source.h"`
   - 添加 `#include "nas_data.h"`

2. **创建定时器更新机制**：
   - 添加 `lv_timer_t *s_update_timer`
   - 创建 `update_timer_cb()` 函数

3. **替换硬编码数据**：
   - 从 `data_source_get_data()` 获取磁盘列表
   - 根据 `disk_count` 动态创建磁盘条
   - 显示磁盘名称、使用率、温度、健康状态

4. **添加状态更新**：
   - 更新网络速度显示
   - 更新连接状态

### 任务 3：Settings_NasTab 保存后切换数据源

**目标**：当用户在 NAS 设置中保存配置后，自动切换数据源并连接

**文件**：`main/ui/screens/ui_Screen_Settings_NasTab.c`

**修改内容**：

1. **添加头文件引用**：
   - 添加 `#include "data_source.h"`

2. **修改 dialog_save_cb 函数**：
   - 在保存配置后调用 `data_source_switch()` 切换到新的 NAS 类型
   - 确保保存的 NAS 类型 ID 正确传递

3. **修改 nas_tab_save 函数**：
   - 在磁盘配置保存时，考虑是否需要重新加载数据源

### 任务 4：添加数据刷新事件机制

**目标**：通过事件总线通知 UI 数据更新

**文件**：`main/data/data_source.c`、`main/ui/ui_events.c`

**修改内容**：

1. **在 data_source 中发布事件**：
   - 在 `data_source_poll()` 成功获取数据后发布 `EVENT_DATA_SOURCE_UPDATED`

2. **在 UI 中订阅事件**：
   - Overview 和 Storage 界面订阅数据更新事件
   - 事件触发时更新 UI 显示

## 三、依赖关系

```
data_source.c ──────┬───> ui_Screen_Overview.c
                    ├───> ui_Screen_Storage.c
                    └───> ui_Screen_Settings_NasTab.c
```

## 四、风险与注意事项

### 4.1 风险

| 风险 | 影响 | 应对措施 |
|------|------|---------|
| data_source 未初始化 | UI 获取数据返回 NULL | 在 UI 更新时添加空指针检查 |
| 数据更新频率过高 | CPU 占用过高 | 使用定时器控制更新频率（建议 5000ms） |
| 磁盘数量不一致 | UI 创建的磁盘条与实际不符 | 动态创建/销毁磁盘条 |
| 线程安全 | data_source 在后台线程更新 | 使用数据副本或互斥锁 |

### 4.2 注意事项

1. **定时器频率**：Overview 使用 5000ms，Storage 使用 5000ms
2. **空指针检查**：所有 `data_source_get_data()` 返回值必须检查
3. **数据范围校验**：百分比、温度等数值需要边界检查（0-100）
4. **内存管理**：动态创建的 UI 对象需要正确销毁

## 五、验证清单

- [ ] Overview 界面 CPU/内存/磁盘/网络数据动态更新
- [ ] Overview 界面 NAS 连接状态正确显示
- [ ] Overview 界面 HDD 指示灯根据实际磁盘状态变化
- [ ] Storage 界面磁盘数据从 data_source 获取
- [ ] Storage 界面磁盘数量根据配置动态调整
- [ ] Settings 保存后数据源自动切换
- [ ] 编译通过，无警告和错误
- [ ] 设备运行正常，无崩溃
