# 项目结构说明（当前架构）

> 本文档描述 `main/data/` 实际实现的架构。早期曾有过基于 `main/nas_adapter/`
> 工厂模式（factory + `switch(type)`）的草案，现已被本套"类型主表 +
> 函数指针虚表"方案取代，旧草案中的 `nas_adapter_*` 接口均已不存在。

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────────────┐
│  消费层 (只读 NasData 快照)                                            │
│   ui/ · utils/rgb_nas_monitor · drivers/fan_control                   │
└───────────────┬───────────────────────────────────────────────────────┘
                │ 订阅 EVENT_NAS_DATA_UPDATED (event_bus)
┌───────────────▼───────────────────────────────────────────────────────┐
│  派发层  utils/event_bus     非阻塞发布/订阅，解耦生产者与消费者        │
└───────────────┬───────────────────────────────────────────────────────┘
                │ data_source_fetch_and_publish()
┌───────────────▼───────────────────────────────────────────────────────┐
│  调度层  data/nas_event_loop.c   FreeRTOS 定时任务，驱动轮询+发布       │
└───────────────┬───────────────────────────────────────────────────────┘
                │ data_source_poll() / data_source_get_data()
┌───────────────▼───────────────────────────────────────────────────────┐
│  抽象层  data/data_source.{h,c}                                        │
│   - DataSource / NasTypeEntry / DataSourceVTable / DataSourceParams   │
│   - NAS_TYPES[] 类型主表（替代 factory switch）                        │
│   - ds_create_by_type() 组包参数、创建、切换（持锁）                    │
└───────────────┬───────────────────────────────────────────────────────┘
                │ NasTypeEntry.create(const DataSourceParams*)
┌───────────────▼───────────────────────────────────────────────────────┐
│  采集层  data/client/*.c         各协议实现，填充 self->data (NasData)  │
└───────────────┬───────────────────────────────────────────────────────┘
                │ #include
┌───────────────▼───────────────────────────────────────────────────────┐
│  契约层  data/nas_data.h       NasData / NasType / 各子结构 (纯数据)    │
│          config/app_info.h     数组长度宏 (MAX_DISKS 等)               │
└─────────────────────────────────────────────────────────────────────┘
```

依赖方向自底向上，契约层 `nas_data.h` 是最底层叶子节点（仅依赖标准库
与 `app_info.h`），不反向依赖任何上层。

## 2. 分层职责

| 层 | 文件 | 职责 | 不做什么 |
|----|------|------|----------|
| 契约层 | `nas_data.h` | 定义 `NasData` 及全部子结构、`NasType` 枚举 | 无逻辑、无 include 业务头 |
| 抽象层 | `data_source.h/.c` | 数据源生命周期、类型主表、参数组包、对外 API | 不含具体协议逻辑 |
| 采集层 | `client/*.c` | 实现各协议的 `create/init/poll/deinit/free` | 不读 `app_cfg`、不碰 UI/锁 |
| 调度层 | `nas_event_loop.c` | 定时任务、驱动轮询、发布事件、生命周期 | 不含采集逻辑、不直接操作锁 |
| 派发层 | `utils/event_bus` | 事件发布/订阅 | 不感知数据结构语义 |
| 消费层 | `ui/ utils/rgb_nas_monitor drivers/fan_control` | 读取 `NasData` 快照渲染/联动 | 不触发采集 |

> 职责边界说明：`nas_event_loop` 只负责"何时驱动 + 发布"，其调用
> `data_source_fetch_and_publish()` 时由 `data_source.c` 在同一持锁序列内
> 完成"抓数 + 取快照 + 发布"。持锁是为保证发布出去的 `const NasData*`
> 指针在被订阅者读取期间不被 `data_source_set_type()` 销毁。两者职责清晰，
> 不在 `nas_event_loop` 内混杂采集与锁逻辑。

## 3. 类型主表机制（替代 factory switch）

`NAS_TYPES[]`（`data_source.c`）是唯一的类型注册表，每项 `NasTypeEntry`
同时携带：

- 身份：`nas_type_enum`(NasType)、`id`(字符串)、`display_name`(界面文案)
- 出厂默认连接参数：`default_ip/port/user/pass/https/comm/baud/poll_sec`
- 能力开关：`has_comm/has_baud/has_user_pass/need_login/is_snmp`
- 采集实现：`create(const DataSourceParams*)` 函数指针

`ds_create_by_type()` 通过 `nas_type_from_string()` 定位表项后调用
`entry->create(&params)`，无需任何 `switch`。**新增一种 NAS 类型 = 在表中
追加一项 + 实现 client，绝不触碰调度层/抽象层其它代码**（开闭原则）。

## 4. DataSourceParams：配置层解耦

client 不再 `#include "app_cfg.h"` 或调用 `app_cfg_get_*`。所有连接参数由
`data_source.c` 的 `ds_create_by_type()` 统一组包为 `DataSourceParams`，
规则为：

- 基础值取自 `NAS_TYPES[]` 出厂默认；
- 运行时 `g_cfg` 值**非空/非 0 才覆盖**（ip/user/pass/comm 判 `&& [0]`，
  数值判 `> 0`）。

`app_cfg` 与采集层之间只通过 `DataSourceParams` 这一个结构化边界通信。

> 注：`snmp_ver` 字段经由 `app_cfg_get_snmp_ver()` 覆盖后传入，链路完整；
> 当前 `snmp_client` 尚未消费该字段（版本协商未实现），保留字段待接入。

## 5. 数据流

1. `nas_event_loop` 任务按 `poll_interval_ms` 周期醒来；
2. 调用 `data_source_poll()` → client 的 `poll()` 填充 `self->data`；
3. `data_source_fetch_and_publish()` 持锁取出 `const NasData*` 并
   `EVENT_NAS_DATA_UPDATED` 发布；
4. 订阅者（UI/外设/驱动）在各自上下文中读取快照，全程非阻塞。

## 6. 新增一个 client 的步骤

1. 在 `client/` 下新建 `<xxx>_client.{c,h}`，实现
   `xxx_client_create(const DataSourceParams*)`（calloc priv、拷贝连接字段）、
   `init`(仅复位状态)、`poll`(填充 `self->data`)、`deinit`、`free`；
2. `poll` 读取 `self->poll_interval_ms` 作为周期；
3. 在 `NAS_TYPES[]` 追加一项，挂上 `.create = xxx_client_create` 与默认参数；
4. 在 `NasType` 枚举与 `nas_type_from_string` 对应分支补充即可（若新增枚举值）。

无需修改工厂、调度层、UI。

## 7. NAS_MOCK：无真实 NAS 的调试模式

早期草案强调"串口本地模拟很重要"，本架构以 `NAS_MOCK` 类型统一覆盖该需求。
选 `mock` 类型时 `mock_client` 不连任何设备，直接在 `poll()` 中生成可配置的
模拟 `NasData`（温度/风扇/磁盘状态等），用于：

- 无硬件时开发/调试 UI 布局与刷新；
- 验证 `fan_control` / `rgb_nas_monitor` 的联动逻辑；
- 演示与截图。

用法：在设置页 NAS 类型选 `Mock (测试)`，其余连接字段留空即可。

## 8. 关键约束

- 所有 client `poll()` 必须**非阻塞**（超时由 client 自行 `poll_ms` 控制）；
- client `priv` 由本模块 `calloc` 并持有，client 不持有全局可变状态；
- 对外暴露的 `data_source_get_data()` 返回 `const NasData*`，消费者只读；
- 切换数据源（`data_source_set_type`）在持锁下销毁旧实例，保证指针安全。
